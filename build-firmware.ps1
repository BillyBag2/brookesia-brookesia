[CmdletBinding()]
param(
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "output/m5stack_tab5",
    [switch]$PackageOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = $PSScriptRoot

function Get-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $ProjectRoot $Path))
}

function Get-CommandVersion([string]$Name, [string[]]$Arguments, [string]$Fallback = "not found") {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command -and -not (Test-Path -LiteralPath $Name -PathType Leaf)) {
        return $Fallback
    }
    try {
        $lines = @(& $Name @Arguments 2>&1)
        if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) {
            return $Fallback
        }
        return [string]$lines[0]
    }
    catch {
        return $Fallback
    }
}

function Get-CMakeCacheValue([string]$CacheFile, [string]$Name) {
    if (-not (Test-Path -LiteralPath $CacheFile -PathType Leaf)) {
        return $null
    }
    foreach ($line in Get-Content -LiteralPath $CacheFile) {
        if ($line -match "^$([regex]::Escape($Name)):[^=]*=(.*)$") {
            return $Matches[1]
        }
    }
    return $null
}

function Get-LockedComponents([string]$LockFile) {
    $components = [ordered]@{}
    $current = $null
    foreach ($line in Get-Content -LiteralPath $LockFile) {
        if ($line -match '^  ([^ ].*):\s*$') {
            $current = $Matches[1]
            continue
        }
        if ($null -ne $current -and $line -match '^    version:\s*["'']?([^"'']+?)["'']?\s*$') {
            $components[$current] = $Matches[1]
            $current = $null
        }
    }
    return $components
}

$BuildRoot = Get-ProjectPath $BuildDirectory
$OutputRoot = Get-ProjectPath $OutputDirectory

if (-not $PackageOnly) {
    if ($null -eq (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw "idf.py was not found. Open an ESP-IDF PowerShell or load the configured ESP-IDF profile first."
    }

    Push-Location $ProjectRoot
    try {
        & idf.py -B $BuildRoot build
        if ($LASTEXITCODE -ne 0) {
            throw "ESP-IDF build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

$FlasherArgsPath = Join-Path $BuildRoot "flasher_args.json"
$ProjectDescriptionPath = Join-Path $BuildRoot "project_description.json"
foreach ($requiredFile in @($FlasherArgsPath, $ProjectDescriptionPath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required ESP-IDF build metadata is missing: $requiredFile"
    }
}

$flasher = Get-Content -Raw -LiteralPath $FlasherArgsPath | ConvertFrom-Json
$description = Get-Content -Raw -LiteralPath $ProjectDescriptionPath | ConvertFrom-Json
if ($null -eq $flasher.flash_files -or @($flasher.flash_files.PSObject.Properties).Count -eq 0) {
    throw "ESP-IDF did not report any flash files in $FlasherArgsPath"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$BuildPrefix = $BuildRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$copiedFiles = [System.Collections.Generic.List[object]]::new()

foreach ($entry in $flasher.flash_files.PSObject.Properties) {
    $relativePath = ([string]$entry.Value).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $BuildRoot $relativePath))
    if (-not $sourcePath.StartsWith($BuildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to package a flash file outside the build directory: $sourcePath"
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Flash file listed by ESP-IDF does not exist: $sourcePath"
    }

    $destinationPath = Join-Path $OutputRoot $relativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
    Copy-Item -Force -LiteralPath $sourcePath -Destination $destinationPath
    $item = Get-Item -LiteralPath $destinationPath
    $copiedFiles.Add([pscustomobject]@{
        Offset = [string]$entry.Name
        Path = $relativePath.Replace('\', '/')
        Size = $item.Length
        Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $destinationPath).Hash.ToLowerInvariant()
    })
}

foreach ($metadataName in @("flasher_args.json", "project_description.json")) {
    Copy-Item -Force -LiteralPath (Join-Path $BuildRoot $metadataName) -Destination (Join-Path $OutputRoot $metadataName)
}
foreach ($sourceName in @("dependencies.lock", "sdkconfig")) {
    $sourcePath = Join-Path $ProjectRoot $sourceName
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        Copy-Item -Force -LiteralPath $sourcePath -Destination (Join-Path $OutputRoot $sourceName)
    }
}

$commit = (& git -C $ProjectRoot rev-parse HEAD).Trim()
$shortCommit = (& git -C $ProjectRoot rev-parse --short=12 HEAD).Trim()
$statusLines = @(& git -C $ProjectRoot status --porcelain --untracked-files=normal)
$isDirty = $statusLines.Count -gt 0
$buildTime = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm:ss 'UTC'")
$components = Get-LockedComponents (Join-Path $ProjectRoot "dependencies.lock")
$cmakeCachePath = Join-Path $BuildRoot "CMakeCache.txt"
$builtPython = Get-CMakeCacheValue $cmakeCachePath "PYTHON"
$builtCMake = Get-CMakeCacheValue $cmakeCachePath "CMAKE_COMMAND"
$builtNinja = Get-CMakeCacheValue $cmakeCachePath "CMAKE_MAKE_PROGRAM"
$idfVersion = if ($components.Contains("idf")) { [string]$components["idf"] } else { [string]$description.git_revision }

if ($null -eq $builtPython -or -not (Test-Path -LiteralPath $builtPython -PathType Leaf)) {
    throw "The Python interpreter used by ESP-IDF was not found in $cmakeCachePath"
}
$mergedImageName = "firmware-complete.bin"
$mergedImagePath = Join-Path $OutputRoot $mergedImageName
$mergeArguments = @(
    "-m", "esptool",
    "--chip", [string]$flasher.extra_esptool_args.chip,
    "merge-bin",
    "--output", $mergedImagePath,
    "--flash-mode", [string]$flasher.flash_settings.flash_mode,
    "--flash-size", [string]$flasher.flash_settings.flash_size,
    "--flash-freq", [string]$flasher.flash_settings.flash_freq,
    "--pad-to-size", [string]$flasher.flash_settings.flash_size
)
foreach ($file in $copiedFiles) {
    $mergeArguments += $file.Offset, (Join-Path $OutputRoot $file.Path)
}
& $builtPython @mergeArguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $mergedImagePath -PathType Leaf)) {
    throw "Failed to create the merged firmware image."
}
$mergedImage = Get-Item -LiteralPath $mergedImagePath
$mergedImageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $mergedImagePath).Hash.ToLowerInvariant()

$localComponents = @()
if ($null -ne $description.build_component_info) {
    foreach ($property in $description.build_component_info.PSObject.Properties) {
        $componentDir = [string]$property.Value.dir
        if ($componentDir.StartsWith($ProjectRoot, [StringComparison]::OrdinalIgnoreCase) -and
                -not $componentDir.Contains("managed_components")) {
            $localComponents += $property.Name
        }
    }
}
$localComponents = @($localComponents | Sort-Object -Unique)

$compilerVersion = "not reported"
if ($description.c_compiler -and (Test-Path -LiteralPath $description.c_compiler -PathType Leaf)) {
    try {
        $compilerLines = @(& $description.c_compiler --version 2>&1)
        if ($compilerLines.Count -gt 0) {
            $compilerVersion = [string]$compilerLines[0]
        }
    }
    catch {
        $compilerVersion = "version query failed: $($_.Exception.Message)"
    }
}

$flashArguments = @()
$flashArguments += "--chip", [string]$flasher.extra_esptool_args.chip
$flashArguments += "--before", [string]$flasher.extra_esptool_args.before
$flashArguments += "--after", [string]$flasher.extra_esptool_args.after
$flashArguments += "write-flash"
$flashArguments += @($flasher.write_flash_args | ForEach-Object { [string]$_ })
foreach ($file in $copiedFiles) {
    $flashArguments += $file.Offset, $file.Path
}
$flashCommand = "python -m esptool --port COM3 " + ($flashArguments -join " ")
$mergedFlashCommand = "python -m esptool --port COM3 --chip $($flasher.extra_esptool_args.chip) " +
    "--before $($flasher.extra_esptool_args.before) --after $($flasher.extra_esptool_args.after) " +
    "write-flash --flash-mode $($flasher.flash_settings.flash_mode) " +
    "--flash-size $($flasher.flash_settings.flash_size) --flash-freq $($flasher.flash_settings.flash_freq) " +
    "0x0 $mergedImageName"
Set-Content -Encoding utf8 -LiteralPath (Join-Path $OutputRoot "flash-command.txt") -Value $mergedFlashCommand
Set-Content -Encoding utf8 -LiteralPath (Join-Path $OutputRoot "flash-files-command.txt") -Value $flashCommand

$readme = [System.Collections.Generic.List[string]]::new()
$readme.Add("# $($description.project_name) firmware package")
$readme.Add("")
$readme.Add("This directory was generated by ``build-firmware.ps1``.")
$readme.Add("")
$readme.Add("## Build provenance")
$readme.Add("")
$readme.Add("| Field | Value |")
$readme.Add("| - | - |")
$readme.Add("| Built | $buildTime |")
$readme.Add("| Git commit | ``$commit`` |")
$readme.Add("| Git short commit | ``$shortCommit`` |")
$readme.Add("| Working tree state | **$(if ($isDirty) { 'dirty' } else { 'clean' })** |")
$readme.Add("| ESP-IDF project version | ``$($description.project_version)`` |")
$readme.Add("| Target | ``$($description.target)`` |")
$readme.Add("| Supported silicon revision | ``$($description.min_rev)`` through ``$($description.max_rev)`` |")
$readme.Add("| SDK config | ``$([IO.Path]::GetFileName([string]$description.config_file))`` |")
$readme.Add("")
if ($isDirty) {
    $readme.Add("### Uncommitted paths")
    $readme.Add("")
    $readme.Add("``````text")
    foreach ($line in $statusLines) { $readme.Add([string]$line) }
    $readme.Add("``````")
    $readme.Add("")
}
$readme.Add("## ESP-IDF tools")
$readme.Add("")
$readme.Add("| Tool | Version |")
$readme.Add("| - | - |")
$readme.Add("| ESP-IDF | $idfVersion (``$($description.git_revision)``) |")
$readme.Add("| Python | $(Get-CommandVersion $builtPython @('--version') $builtPython) |")
$readme.Add("| CMake | $(Get-CommandVersion $builtCMake @('--version') $builtCMake) |")
$readme.Add("| Ninja | $(Get-CommandVersion $builtNinja @('--version') $builtNinja) |")
$readme.Add("| C compiler | $compilerVersion |")
$readme.Add("| ESP-IDF path | ``$($description.idf_path)`` |")
$readme.Add("")
$readme.Add("## Managed components")
$readme.Add("")
$readme.Add("Exact resolved versions are copied from ``dependencies.lock``.")
$readme.Add("")
$readme.Add("| Component | Version |")
$readme.Add("| - | - |")
foreach ($component in $components.GetEnumerator()) {
    $readme.Add("| ``$($component.Key)`` | ``$($component.Value)`` |")
}
$readme.Add("")
$readme.Add("## Project components")
$readme.Add("")
$readme.Add("These components were built from this repository at the commit above:")
$readme.Add("")
foreach ($component in $localComponents) { $readme.Add("- ``$component``") }
$readme.Add("")
$readme.Add("## Flash files")
$readme.Add("")
$readme.Add("### Single-file burner image")
$readme.Add("")
$readme.Add("``$mergedImageName`` is a merged, 16 MB image padded with erased-flash bytes. It contains the bootloader, partition table, application, and LittleFS image at their ESP-IDF-defined offsets. Configure a burner to write this file at flash offset ``0x0``.")
$readme.Add("")
$readme.Add("| File | Flash offset | Bytes | SHA-256 |")
$readme.Add("| - | - | -: | - |")
$readme.Add("| ``$mergedImageName`` | ``0x0`` | $($mergedImage.Length) | ``$mergedImageHash`` |")
$readme.Add("")
$readme.Add("### Individual ESP-IDF images")
$readme.Add("")
$readme.Add("| Offset | File | Bytes | SHA-256 |")
$readme.Add("| - | - | -: | - |")
foreach ($file in $copiedFiles) {
    $readme.Add("| ``$($file.Offset)`` | ``$($file.Path)`` | $($file.Size) | ``$($file.Sha256)`` |")
}
$readme.Add("")
$readme.Add("With the ESP-IDF environment active, replace ``COM3`` if necessary and flash the merged image with:")
$readme.Add("")
$readme.Add("``````powershell")
$readme.Add($mergedFlashCommand)
$readme.Add("``````")
$readme.Add("")
$readme.Add("The merged-image command is saved in ``flash-command.txt``. The original multi-file ESP-IDF command is retained in ``flash-files-command.txt``.")

Set-Content -Encoding utf8 -LiteralPath (Join-Path $OutputRoot "README.md") -Value $readme
Write-Host "Firmware package created at $OutputRoot"
