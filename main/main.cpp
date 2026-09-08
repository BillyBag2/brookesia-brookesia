/*
 * SPDX-License-Identifier: CC0-1.0
 */
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "boost/chrono.hpp"
#include "boost/json/array.hpp"
#include "boost/json/value.hpp"
#include "boost/thread.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils/check.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/lib_utils/log.hpp"
#include "brookesia/lib_utils/thread_config.hpp"
#include "brookesia/service_helper/media/display.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "brookesia/system_super.hpp"
#include "sdkconfig.h"

#undef BROOKESIA_LOG_TAG
#define BROOKESIA_LOG_TAG "Main"

using namespace esp_brookesia;

namespace {

using DisplayHelper = service::helper::Display;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;

struct DisplayInfo {
    uint32_t id;
    uint32_t width;
    uint32_t height;
};

std::expected<DisplayInfo, std::string> start_display()
{
    if (!DisplayHelper::is_available()) {
        return std::unexpected("Display service is unavailable");
    }

    static auto display_binding =
        service::ServiceManager::get_instance().bind(DisplayHelper::get_name().data());
    if (!display_binding.is_valid()) {
        return std::unexpected("Failed to bind the Display service");
    }

    auto outputs_json = DisplayHelper::call_function_sync<boost::json::array>(
                            DisplayHelper::FunctionId::GetOutputs,
                            service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                        );
    if (!outputs_json.has_value()) {
        return std::unexpected("Failed to query display outputs: " + outputs_json.error());
    }

    std::vector<DisplayHelper::OutputInfo> outputs;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(boost::json::value(outputs_json.value()), outputs)) {
        return std::unexpected("Failed to parse display outputs");
    }
    if (outputs.empty() || (outputs.front().width == 0) || (outputs.front().height == 0)) {
        return std::unexpected("No usable display output was found");
    }

    const auto &output = outputs.front();
    gui::lvgl::DisplaySourceConfig source_config{};
    source_config.task_core_id = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_INIT_THREAD_CORE_ID;
    if (!gui::lvgl::DisplaySource::get_instance().start(source_config)) {
        return std::unexpected("Failed to start the LVGL display source");
    }

    auto active_result = DisplayHelper::call_function_sync(
                             DisplayHelper::FunctionId::SetActiveSourceRole,
                             std::string(),
                             std::string(gui::lvgl::DISPLAY_SOURCE_ROLE),
                             service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                         );
    if (!active_result.has_value()) {
        return std::unexpected("Failed to activate the LVGL source: " + active_result.error());
    }

    DisplayHelper::TouchGestureConfig gesture_config{};
    gesture_config.enabled = true;
    auto gesture_result = DisplayHelper::call_function_sync(
                              DisplayHelper::FunctionId::SetTouchGestureConfig,
                              static_cast<double>(output.id),
                              BROOKESIA_DESCRIBE_TO_JSON(gesture_config).as_object(),
                              service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                          );
    if (!gesture_result.has_value()) {
        return std::unexpected("Failed to enable touch gestures: " + gesture_result.error());
    }

    if (output.backlight.has_value()) {
        auto backlight_result = DisplayHelper::call_function_async(
                                    DisplayHelper::FunctionId::SetBacklightOnOff, output.id, true
                                );
        if (!backlight_result) {
            return std::unexpected("Failed to turn on the display backlight");
        }
    }

    BROOKESIA_LOGI("Display ready: %1%x%2%", output.width, output.height);
    return DisplayInfo{output.id, output.width, output.height};
}

void bring_up_brookesia()
{
    auto &service_manager = service::ServiceManager::get_instance();
    if (!service_manager.init() || !service_manager.start()) {
        BROOKESIA_LOGE("Failed to start the Brookesia service manager");
        return;
    }

    auto display = start_display();
    if (!display.has_value()) {
        BROOKESIA_LOGE("Display bring-up failed: %1%", display.error());
        return;
    }

    static std::unique_ptr<system::super::System> system_instance;
    system_instance = std::make_unique<system::super::System>();

    system::super::System::Config config;
    config.core_config.gui_backend = std::make_unique<gui::lvgl::Backend>();
    config.core_config.environment = {
        .width_px = static_cast<int32_t>(display->width),
        .height_px = static_cast<int32_t>(display->height),
        .density = 1.0F,
        .font_scale = 1.0F,
    };

    auto init_result = system_instance->init(std::move(config));
    if (!init_result.has_value()) {
        BROOKESIA_LOGE("Brookesia system initialization failed: %1%", init_result.error());
        return;
    }

    auto start_result = system_instance->start();
    if (!start_result.has_value()) {
        BROOKESIA_LOGE("Brookesia system startup failed: %1%", start_result.error());
        return;
    }

    BROOKESIA_LOGI("ESP-Brookesia is running");
}

} // namespace

extern "C" void app_main(void)
{
    BROOKESIA_LOGI("Starting ESP-Brookesia");

    BROOKESIA_THREAD_CONFIG_GUARD({
        .stack_size = 40 * 1024,
    });
    boost::thread(bring_up_brookesia).detach();
}
