[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$videoKconfigPath = Join-Path $PSScriptRoot "managed_components/espressif__esp_video/Kconfig"
$h264KconfigPath = Join-Path $PSScriptRoot "managed_components/espressif__esp_h264/Kconfig"
$symbol = "ESP_VIDEO_USE_CUSTOMIZED_ESP_H264_VERSION"

if (-not (Test-Path -LiteralPath $videoKconfigPath -PathType Leaf)) {
    Write-Warning "esp_video is not installed yet. ESP-IDF will download it during dependency resolution."
    Write-Warning "If configuration then reports the missing $symbol symbol, rerun this script and the build."
    exit 0
}

$oldH264Block = @"

    # Compatibility selector referenced by current component-registry metadata
    # but absent from esp_h264 1.3.8.
    config $symbol
        bool
        default n
"@
if (Test-Path -LiteralPath $h264KconfigPath -PathType Leaf) {
    $h264Content = [IO.File]::ReadAllText($h264KconfigPath)
    if ($h264Content.Contains($oldH264Block)) {
        $h264Content = $h264Content.Replace($oldH264Block, "")
        [IO.File]::WriteAllText($h264KconfigPath, $h264Content, [Text.UTF8Encoding]::new($false))
        Write-Host "Removed the obsolete esp_h264 Kconfig patch."
    }
}

$content = [IO.File]::ReadAllText($videoKconfigPath)
if ($content.Contains("config $symbol")) {
    Write-Host "esp_video Kconfig compatibility patch is already applied."
} else {
    $anchor = '    rsource "./src/data_reprocessing/Kconfig.data_reprocessing"'
    if (-not $content.Contains($anchor)) {
        throw "The esp_video Kconfig layout is not recognized; refusing to patch $videoKconfigPath."
    }

    $addition = @"
    config $symbol
        bool "Use Customized esp_h264 Version"
        default n
        help
            When disabled, use the esp_h264 version defined in
            esp_video/idf_component.yml.

            When enabled, ignore the esp_h264 version in
            esp_video/idf_component.yml, and use the esp_h264 version
            defined in another component's or main's idf_component.yml.

$anchor
"@
    $content = $content.Replace($anchor, $addition)
    [IO.File]::WriteAllText($videoKconfigPath, $content, [Text.UTF8Encoding]::new($false))
    Write-Host "Applied esp_video Kconfig compatibility patch."
}

$touchSourcePath = Join-Path $PSScriptRoot "managed_components/espressif__esp_board_manager/devices/dev_lcd_touch/dev_lcd_touch_sub_i2c.c"
if (-not (Test-Path -LiteralPath $touchSourcePath -PathType Leaf)) {
    Write-Warning "esp_board_manager is not installed yet; skipping the LCD touch startup patch."
    exit 0
}

$touchContent = [IO.File]::ReadAllText($touchSourcePath)
$touchMarker = "LCD_TOUCH_PROBE_ATTEMPTS"
if ($touchContent.Contains($touchMarker)) {
    Write-Host "LCD touch startup retry patch is already applied."
} else {
$includeAnchor = '#include "esp_log.h"'
$includeReplacement = @"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
"@
$probeAnchor = @"
    for (size_t i = 0; i < touch_cfg->sub_cfg.i2c.i2c_addr_count; i++) {
        uint16_t candidate = touch_cfg->sub_cfg.i2c.i2c_addr[i];
        if (candidate == 0) {
            continue;
        }
        if ((candidate & 0x1) != 0 || candidate > 0xfe) {
            ESP_LOGW(TAG, "Skip invalid 8-bit I2C address: 0x%02x", candidate);
            continue;
        }
        uint16_t candidate_runtime_addr = candidate >> 1;
        ret = i2c_master_probe((i2c_master_bus_handle_t)i2c_bus_handle, candidate_runtime_addr, 200);
        if (ret == ESP_OK) {
            effective_addr = candidate;
            runtime_addr = candidate_runtime_addr;
            break;
        }
    }
"@
$probeReplacement = @"
    // The touch controller is powered immediately before this function is called.
    // Allow it to boot, then retry to tolerate controller and rail startup variance.
    enum { LCD_TOUCH_PROBE_ATTEMPTS = 6, LCD_TOUCH_PROBE_DELAY_MS = 20 };
    vTaskDelay(pdMS_TO_TICKS(LCD_TOUCH_PROBE_DELAY_MS));
    for (size_t attempt = 0; attempt < LCD_TOUCH_PROBE_ATTEMPTS && effective_addr == 0; attempt++) {
        for (size_t i = 0; i < touch_cfg->sub_cfg.i2c.i2c_addr_count; i++) {
            uint16_t candidate = touch_cfg->sub_cfg.i2c.i2c_addr[i];
            if (candidate == 0) {
                continue;
            }
            if ((candidate & 0x1) != 0 || candidate > 0xfe) {
                ESP_LOGW(TAG, "Skip invalid 8-bit I2C address: 0x%02x", candidate);
                continue;
            }
            uint16_t candidate_runtime_addr = candidate >> 1;
            ret = i2c_master_probe((i2c_master_bus_handle_t)i2c_bus_handle, candidate_runtime_addr, 200);
            if (ret == ESP_OK) {
                effective_addr = candidate;
                runtime_addr = candidate_runtime_addr;
                break;
            }
        }
        if (effective_addr == 0 && attempt + 1 < LCD_TOUCH_PROBE_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(LCD_TOUCH_PROBE_DELAY_MS));
        }
    }
"@

if (-not $touchContent.Contains($includeAnchor) -or -not $touchContent.Contains($probeAnchor)) {
    throw "The esp_board_manager LCD touch source layout is not recognized; refusing to patch $touchSourcePath."
}

$touchContent = $touchContent.Replace($includeAnchor, $includeReplacement)
$touchContent = $touchContent.Replace($probeAnchor, $probeReplacement)
[IO.File]::WriteAllText($touchSourcePath, $touchContent, [Text.UTF8Encoding]::new($false))
Write-Host "Applied LCD touch startup retry patch."
}

# Registry releases 0.8.3 lack the battery UI implementation present in the
# pinned fork. Apply only its battery changes; do not pull in newer expansion
# service code, which is incompatible with the pinned Device/Helper pair.
$patches = @(
    "patches/brookesia/settings-battery.patch",
    "patches/brookesia/superos-battery.patch"
)
foreach ($relativePatch in $patches) {
    $patchPath = Join-Path $PSScriptRoot $relativePatch
    if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
        throw "Managed-component patch is missing: $patchPath"
    }

    $ErrorActionPreference = "Continue"
    & git -C $PSScriptRoot apply --check $patchPath 2>$null
    $checkExitCode = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($checkExitCode -eq 0) {
        & git -C $PSScriptRoot apply $patchPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply managed-component patch: $patchPath"
        }
        Write-Host "Applied $relativePatch."
        continue
    }

    $ErrorActionPreference = "Continue"
    & git -C $PSScriptRoot apply --reverse --check $patchPath 2>$null
    $reverseCheckExitCode = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($reverseCheckExitCode -eq 0) {
        Write-Host "$relativePatch is already applied."
        continue
    }

    throw "Managed components do not match the expected release or patched source for $relativePatch."
}
