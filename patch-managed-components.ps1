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
    exit 0
}

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
