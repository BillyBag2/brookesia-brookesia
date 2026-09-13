#pragma once

namespace esp_brookesia::board {

struct AppearanceConfig {
    float density;
    float font_scale;
};

[[nodiscard]] constexpr AppearanceConfig appearance_config() noexcept
{
    return {
        .density = 1.5F,
        .font_scale = 1.0F,
    };
}

} // namespace esp_brookesia::board
