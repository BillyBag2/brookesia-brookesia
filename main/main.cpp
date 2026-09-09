/*
 * SPDX-License-Identifier: CC0-1.0
 */
#include <algorithm>
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
#include "esp_board_manager.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "sdkconfig.h"

#undef BROOKESIA_LOG_TAG
#define BROOKESIA_LOG_TAG "Main"

using namespace esp_brookesia;

namespace {

using DisplayHelper = service::helper::Display;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;
constexpr uint16_t SHELL_VERTICAL_EDGE_GESTURE_PX = 24;

#if CONFIG_APP_TOUCH_DEBUG_OVERLAY
struct TouchDebugOverlay {
    lv_obj_t *marker = nullptr;
    lv_obj_t *label = nullptr;
};

TouchDebugOverlay touch_debug_overlay;

void update_touch_debug_overlay(lv_timer_t *)
{
    lv_indev_t *pointer = nullptr;
    for (auto *indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if ((lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) &&
                (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)) {
            pointer = indev;
            break;
        }
    }

    if (pointer == nullptr) {
        lv_obj_add_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_debug_overlay.label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_point_t point{};
    lv_indev_get_point(pointer, &point);

    constexpr int32_t marker_size = 44;
    constexpr int32_t label_width = 150;
    constexpr int32_t label_height = 38;
    const int32_t display_width = lv_display_get_horizontal_resolution(nullptr);
    const int32_t display_height = lv_display_get_vertical_resolution(nullptr);
    const int32_t label_x = std::clamp<int32_t>(point.x + 24, 0, display_width - label_width);
    const int32_t label_y = std::clamp<int32_t>(point.y - 48, 0, display_height - label_height);

    lv_obj_set_pos(touch_debug_overlay.marker, point.x - marker_size / 2, point.y - marker_size / 2);
    lv_obj_set_pos(touch_debug_overlay.label, label_x, label_y);
    lv_label_set_text_fmt(touch_debug_overlay.label, "x:%ld  y:%ld", point.x, point.y);
    lv_obj_remove_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(touch_debug_overlay.label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(touch_debug_overlay.marker);
    lv_obj_move_foreground(touch_debug_overlay.label);
}

bool start_touch_debug_overlay()
{
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        return false;
    }

    auto *top_layer = lv_layer_top();
    touch_debug_overlay.marker = lv_obj_create(top_layer);
    lv_obj_set_size(touch_debug_overlay.marker, 44, 44);
    lv_obj_set_style_radius(touch_debug_overlay.marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(touch_debug_overlay.marker, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(touch_debug_overlay.marker, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(touch_debug_overlay.marker, 4, 0);
    lv_obj_remove_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_debug_overlay.marker, LV_OBJ_FLAG_FLOATING);

    touch_debug_overlay.label = lv_label_create(top_layer);
    lv_obj_set_size(touch_debug_overlay.label, 150, 38);
    lv_obj_set_style_bg_color(touch_debug_overlay.label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(touch_debug_overlay.label, LV_OPA_80, 0);
    lv_obj_set_style_text_color(touch_debug_overlay.label, lv_color_white(), 0);
    lv_obj_set_style_pad_all(touch_debug_overlay.label, 7, 0);
    lv_obj_remove_flag(touch_debug_overlay.label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch_debug_overlay.label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_debug_overlay.label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_debug_overlay.label, LV_OBJ_FLAG_FLOATING);

    lv_timer_create(update_touch_debug_overlay, 20, nullptr);
    esp_lv_adapter_unlock();
    return true;
}
#endif

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

#if CONFIG_ESP_BOARD_M5STACK_TAB5
    // The second Tab5 I/O expander drives WLAN_PWR_EN on P0. It is otherwise
    // unused by the active services, so Board Manager will not initialize it.
    auto wifi_power_result = esp_board_manager_init_device_by_name("gpio_expander_2");
    if (wifi_power_result != ESP_OK) {
        BROOKESIA_LOGE("Failed to enable the Tab5 Wi-Fi coprocessor power: %1%", esp_err_to_name(wifi_power_result));
    } else {
        BROOKESIA_LOGI("Tab5 Wi-Fi coprocessor power enabled");
    }
#endif

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
        // Scale dp/sp UI metrics for the Tab5's high-density 5-inch panel.
        // The framebuffer and pixel-sized media remain at the native resolution.
        .density = 1.5F,
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

    // The shell defaults its top/bottom edge gesture zone to 8% of the
    // display height (96 px on the portrait Tab5).  That covers the Settings
    // back button and lets the status-peek recognizer claim its first touch.
    // Keep edge gestures available, but require them to begin at the actual
    // screen edge.
    DisplayHelper::TouchGestureConfig gesture_config{};
    gesture_config.enabled = true;
    gesture_config.threshold.horizontal_edge =
        static_cast<uint16_t>(std::clamp<uint32_t>(display->width * 6 / 100, 24, 96));
    gesture_config.threshold.vertical_edge = SHELL_VERTICAL_EDGE_GESTURE_PX;
    auto gesture_result = DisplayHelper::call_function_sync(
                              DisplayHelper::FunctionId::SetTouchGestureConfig,
                              static_cast<double>(display->id),
                              BROOKESIA_DESCRIBE_TO_JSON(gesture_config).as_object(),
                              service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                          );
    if (!gesture_result.has_value()) {
        BROOKESIA_LOGW("Failed to narrow the shell edge gesture zone: %1%", gesture_result.error());
    }

#if CONFIG_APP_TOUCH_DEBUG_OVERLAY
    if (!start_touch_debug_overlay()) {
        BROOKESIA_LOGW("Failed to start the touch debug overlay");
    } else {
        BROOKESIA_LOGI("Touch debug overlay enabled");
    }
#endif

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
