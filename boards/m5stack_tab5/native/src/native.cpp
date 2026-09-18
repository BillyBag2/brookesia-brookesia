#include "brookesia/board/native.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/power/battery.hpp"
#include "driver/i2c_master.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "esp_io_expander.h"

namespace esp_brookesia::board {

namespace {

constexpr char I2C_PERIPHERAL_NAME[] = "i2c_master";
constexpr char POWER_EXPANDER_NAME[] = "gpio_expander_2";
constexpr uint8_t INA226_ADDRESS = 0x41;
constexpr uint32_t CHARGE_ENABLE_PIN = 1U << 7;
constexpr uint32_t CHARGE_STATUS_PIN = 1U << 6;
constexpr uint32_t QUICK_CHARGE_PIN = 1U << 5;
constexpr uint32_t I2C_TIMEOUT_MS = 100;

// Open-circuit voltage curve for a conventional two-cell Li-ion pack. Runtime
// load and charging introduce some error, but this needs no learned state.
constexpr std::array<std::pair<uint16_t, uint8_t>, 12> BATTERY_CURVE = {{
    {6000, 0}, {6400, 5}, {6800, 10}, {7000, 20}, {7200, 30}, {7400, 40},
    {7520, 50}, {7640, 60}, {7760, 70}, {7900, 80}, {8100, 90}, {8400, 100},
}};

uint8_t voltage_to_percentage(uint32_t voltage_mv)
{
    if (voltage_mv <= BATTERY_CURVE.front().first) {
        return BATTERY_CURVE.front().second;
    }
    for (size_t i = 1; i < BATTERY_CURVE.size(); ++i) {
        const auto [upper_mv, upper_percent] = BATTERY_CURVE[i];
        if (voltage_mv <= upper_mv) {
            const auto [lower_mv, lower_percent] = BATTERY_CURVE[i - 1];
            return static_cast<uint8_t>(lower_percent +
                                        ((voltage_mv - lower_mv) * (upper_percent - lower_percent)) /
                                            (upper_mv - lower_mv));
        }
    }
    return 100;
}

class Tab5Battery final : public hal::power::BatteryIface {
public:
    static constexpr char NAME[] = "M5StackTab5:Battery";

    Tab5Battery(i2c_master_dev_handle_t monitor, esp_io_expander_handle_t expander)
        : BatteryIface({
              .name = "M5Stack TAB5 two-cell battery",
              .chemistry = "Li-ion 2S",
              .abilities = {
                  Ability::Voltage,
                  Ability::Current,
                  Ability::Percentage,
                  Ability::PowerSource,
                  Ability::ChargeState,
                  Ability::ChargerControl,
                  Ability::ChargeConfig,
              },
          })
        , monitor_(monitor)
        , expander_(expander)
    {
    }

    bool get_state(State &state) override
    {
        uint16_t shunt_value = 0;
        uint16_t bus_raw = 0;
        uint32_t pins = 0;
        if (!read_register(0x01, shunt_value) ||
                !read_register(0x02, bus_raw) ||
                esp_io_expander_get_level(expander_, CHARGE_STATUS_PIN, &pins) != ESP_OK) {
            return false;
        }

        // INA226: bus-voltage LSB is 1.25 mV; shunt-voltage LSB is 2.5 uV.
        // With the TAB5's 5 mOhm shunt that is 0.5 mA per raw count. Hardware
        // polarity is inverted here so positive means charging, matching the HAL.
        const uint32_t voltage_mv = (static_cast<uint32_t>(bus_raw) * 5U) / 4U;
        const int16_t shunt_raw = static_cast<int16_t>(shunt_value);
        const int32_t current_ma = -(static_cast<int32_t>(shunt_raw) / 2);
        const bool charging = (pins & CHARGE_STATUS_PIN) == 0;
        state = {
            .is_present = voltage_mv >= 5000,
            .power_source = charging ? PowerSource::External : PowerSource::Battery,
            .charge_state = charging ? ChargeState::Charging : ChargeState::NotCharging,
            .level_source = LevelSource::VoltageCurve,
            .voltage_mv = voltage_mv,
            .current_ma = current_ma,
            .percentage = voltage_to_percentage(voltage_mv),
            .vbus_voltage_mv = std::nullopt,
            .system_voltage_mv = std::nullopt,
            .is_low = voltage_mv <= 6800,
            .is_critical = voltage_mv <= 6400,
        };
        return true;
    }

    bool get_charge_config(ChargeConfig &config) override
    {
        uint32_t pins = 0;
        if (esp_io_expander_get_level(
                expander_, CHARGE_ENABLE_PIN | QUICK_CHARGE_PIN, &pins
            ) != ESP_OK) {
            return false;
        }
        config = {
            .enabled = (pins & CHARGE_ENABLE_PIN) != 0,
            .target_voltage_mv = 8400,
            .charge_current_ma = (pins & QUICK_CHARGE_PIN) ? 500U : 1000U,
            .precharge_current_ma = 0,
            .termination_current_ma = 0,
        };
        return true;
    }

    bool set_charge_config(const ChargeConfig &config) override
    {
        if ((config.charge_current_ma != 500 && config.charge_current_ma != 1000) ||
                (config.target_voltage_mv != 0 && config.target_voltage_mv != 8400) ||
                config.precharge_current_ma != 0 || config.termination_current_ma != 0) {
            return false;
        }
        // nCHG_QC_EN is active low: low selects the 1 A charging rate.
        if (esp_io_expander_set_level(
                expander_, QUICK_CHARGE_PIN, config.charge_current_ma == 1000 ? 0 : 1
            ) != ESP_OK) {
            return false;
        }
        return set_charging_enabled(config.enabled);
    }

    bool set_charging_enabled(bool enabled) override
    {
        return esp_io_expander_set_level(expander_, CHARGE_ENABLE_PIN, enabled ? 1 : 0) == ESP_OK;
    }

private:
    bool read_register(uint8_t reg, uint16_t &value) const
    {
        uint8_t bytes[2] = {};
        if (i2c_master_transmit_receive(monitor_, &reg, 1, bytes, sizeof(bytes), I2C_TIMEOUT_MS) != ESP_OK) {
            return false;
        }
        value = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
        return true;
    }

    i2c_master_dev_handle_t monitor_ = nullptr;
    esp_io_expander_handle_t expander_ = nullptr;
};

class Tab5PowerDevice final : public hal::Device {
public:
    static constexpr char NAME[] = "M5StackTab5Power";

    Tab5PowerDevice()
        : Device(NAME)
    {
    }

    bool probe() override
    {
        return esp_board_manager_check_name(POWER_EXPANDER_NAME);
    }

    std::vector<hal::InterfaceSpec> get_interface_specs() const override
    {
        return {{hal::power::BatteryIface::NAME, Tab5Battery::NAME}};
    }

    bool on_init() override
    {
        void *bus = nullptr;
        if (esp_board_periph_ref_handle(I2C_PERIPHERAL_NAME, &bus) != ESP_OK || bus == nullptr) {
            return false;
        }
        bus_referenced_ = true;

        auto *expander_handle = static_cast<esp_io_expander_handle_t *>(nullptr);
        if (esp_board_device_get_handle(POWER_EXPANDER_NAME, reinterpret_cast<void **>(&expander_handle)) != ESP_OK ||
                expander_handle == nullptr || *expander_handle == nullptr) {
            on_deinit();
            return false;
        }
        expander_ = *expander_handle;

        // The current TAB5 BSP snapshot configures P6 as an output. CHG_STAT is
        // a charger status input, so correct its direction before sampling it.
        if (esp_io_expander_set_dir(expander_, CHARGE_STATUS_PIN, IO_EXPANDER_INPUT) != ESP_OK) {
            on_deinit();
            return false;
        }

        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = INA226_ADDRESS,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
            .flags = {},
        };
        if (i2c_master_bus_add_device(static_cast<i2c_master_bus_handle_t>(bus), &config, &monitor_) != ESP_OK) {
            on_deinit();
            return false;
        }

        interfaces_.emplace(Tab5Battery::NAME, std::make_shared<Tab5Battery>(monitor_, expander_));
        return true;
    }

    void on_deinit() override
    {
        interfaces_.clear();
        if (monitor_ != nullptr) {
            i2c_master_bus_rm_device(monitor_);
            monitor_ = nullptr;
        }
        expander_ = nullptr;
        if (bus_referenced_) {
            esp_board_periph_unref_handle(I2C_PERIPHERAL_NAME);
            bus_referenced_ = false;
        }
    }

private:
    i2c_master_dev_handle_t monitor_ = nullptr;
    esp_io_expander_handle_t expander_ = nullptr;
    bool bus_referenced_ = false;
};

BROOKESIA_PLUGIN_REGISTER(hal::Device, Tab5PowerDevice, std::string(Tab5PowerDevice::NAME));

} // namespace

bool prepare_native_hardware()
{
    // The second Tab5 I/O expander drives WLAN_PWR_EN on P0. It is otherwise
    // unused by the active services, so Board Manager will not initialize it.
    return esp_board_manager_init_device_by_name("gpio_expander_2") == ESP_OK;
}

} // namespace esp_brookesia::board
