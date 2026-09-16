#include "brookesia/board/native.hpp"

#include "esp_board_manager.h"

namespace esp_brookesia::board {

bool prepare_native_hardware()
{
    // The second Tab5 I/O expander drives WLAN_PWR_EN on P0. It is otherwise
    // unused by the active services, so Board Manager will not initialize it.
    return esp_board_manager_init_device_by_name("gpio_expander_2") == ESP_OK;
}

} // namespace esp_brookesia::board
