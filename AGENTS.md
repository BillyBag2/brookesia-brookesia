# ESP-IDF commands

## Repository role and boundaries

This directory is the `brookesia-brookesia` ESP-IDF firmware submodule of the
parent `brookesia-brookesia-wasm` repository. Keep commits separated by
repository: commit firmware, board-profile, application, and resource-override
changes here first, then commit the updated submodule pointer in the parent.
Do not stage or commit generated parent build output while working here.

The parent Emscripten build does not compile this project's
`managed_components/` tree. It assembles pinned Brookesia sources under the
parent's `.deps/assembled/` directory. It does, however, deliberately consume
the following project-owned files directly from this submodule:

- `boards/<board>/common/` for appearance settings shared by native and WASM;
- `resource_overrides/` for sparse application and SuperOS resource changes;
- selected portable application sources when they have an explicit host CMake
  integration.

Treat `.deps/`, `managed_components/`, `components/gen_bmgr_codes/`, `littlefs/`,
and all `build/` directories according to their provenance. Do not copy fixes
into generated dependency trees as the only source of a change.

## Board-specific code

The selected native board is controlled by `SDKCONFIG_DEFAULTS` in the root
`CMakeLists.txt`; it currently defaults to `m5stack_tab5`. Keep board concerns
in these layers:

- `boards/<board>/common/include/brookesia/board/config.hpp`: portable appearance
  values used by both native and WASM, such as density and font scale;
- `boards/<board>/native/`: ESP-IDF-only hardware preparation;
- `components/brookesia_board_target/`: resolves the generated Board Manager
  target and exposes the selected common/native implementation;
- `components/gen_bmgr_codes/`: generated Board Manager output, not the place
  for hand-maintained policy.

Do not move physical BSP properties already supplied by Board Manager into the
portable appearance profile. The parent repository maintains its own
`boards/<board>/wasm/` hardware description for browser-only behaviour.

## Read the connected chip revision

Run these commands in an ESP-IDF PowerShell terminal. For this workspace's
configured ESP-IDF installation, initialize a regular PowerShell terminal with:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
```

Use the installed profile rather than calling `export.ps1` after manually
setting `IDF_TOOLS_PATH`; the latter can make ESP-IDF search for a second nested
`tools` directory and report that every tool is missing.

List serial ports, then query the board (replace `COM3` with its port):

```powershell
python -m serial.tools.list_ports
python -m esptool --port COM3 chip-id
```

Read the `revision vX.Y` value in the detected chip information. This queries the
chip without flashing firmware. Close any serial monitor using the port first.

For older ESP-IDF environments with esptool v4, use the underscore command:

```powershell
python -m esptool --port COM3 chip_id
```

Reference: [Espressif chip compatibility guidance](https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md).

## Build, flash, and monitor

Run ESP-IDF commands from the repository root. The installed Espressif PowerShell
profile is the quickest way to initialize a fresh terminal:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
idf.py build
```

This project is configured for `m5stack_tab5`/ESP32-P4. The active `sdkconfig` takes precedence over defaults
such as `sdkconfig.m5_stack_tab5`; when changing a default for future clean builds,
also check whether the active `sdkconfig` still contains an older value.

Flash the firmware, partition table, bootloader, and the LittleFS app/resources
image together with:

```powershell
idf.py -p COM3 flash
```

Start or reattach the monitor with:

```powershell
idf.py -p COM3 monitor
```

Only one process can own COM3. Stop the VS Code serial monitor before querying the
chip or flashing. Exit `idf.py monitor` with `Ctrl+]`.

Useful build notes:

- `idf.py build` regenerates `build/littlefs_data.bin`; app/resource changes may
  therefore require flashing LittleFS as well as the application binary. A normal
  `idf.py flash` does this for the partitions marked `FLASH_IN_PROJECT`.
- Component Manager evaluates conditional dependencies during CMake configuration.
  If `sdkconfig`, a component manifest, or conditional dependencies change and the
  old graph remains in `dependencies.lock`/`build`, use `idf.py fullclean` and
  regenerate the lockfile before diagnosing missing headers.
- Do not hand-edit generated files under `build/`.
- A GUI descriptor must name an existing `screenFlow`. Each native app JSON root
  needs a `screenFlow` asset whose id matches `descriptor.screen_flows`; otherwise
  launch fails with `App GUI screen flow is not found`.
- The connected Tab5 contains an ESP32-P4 revision v1.0. ESP-SR 2.4.4 rejects
  pre-v3 ESP32-P4 builds under ESP-IDF 6.1, so do not enable the Brookesia AV/audio
  processor merely to obtain volume control. The current audio examples use the
  shared `AudioDecoder0`, `AudioEncoder0`, and `AudioPlayback` services. The local
  `brookesia_service_audio` codec fallbacks adapt those services to the ES8388 and
  ES7210 interfaces while the processor remains unavailable.

For a faster rebuild, ccache can be kept inside this workspace:

```powershell
$env:CCACHE_DIR = "$PWD\.ccache"
$env:CCACHE_TEMPDIR = "$PWD\.ccache\tmp"
idf.py build
```

## Resources and registered applications

`littlefs/` is a generated/staged native filesystem tree. A native configure or
build may recreate files within it. Persistent project customizations belong in
`resource_overrides/` at the same relative path they have under `littlefs/`.
`cmake/resource_overrides.cmake` overlays that sparse tree after normal component
resource staging in both the native build and the parent WASM build.

When changing a managed application's JSON, image, or font:

1. Put the authoritative changed file under `resource_overrides/`.
2. Update the checked-in `littlefs/` copy when the repository intentionally
   records the current staged native image.
3. Rebuild before testing; editing an old file under a build directory is not
   sufficient.
4. Flash the LittleFS partition as well as the application when testing native
   resources.

Native application-owned packages live beside their component source and are
staged with `brookesia_stage_runtime_app_package`. An app launcher icon needs all
of the following:

- `manifest.icon_id`, `manifest.icon_path`, and the correct `resource_dir`;
- a package `res/images/index.json` containing the matching image id;
- the referenced image file in that directory;
- a staging rule for the package.

The existing launcher icons are 92 x 92 RGB JPEG files. Preserve those dimensions
unless the shell's launcher specification changes. A resource being present in
LittleFS does not by itself make an app available in WASM: the parent build also
needs to compile/register that app and include it in the selected board's
`wasm/apps.cmake` allowlist.

## Audio application architecture

Applications should use Brookesia services rather than acquire ES8388, ES7210,
I2S, or codec HAL objects directly. Current responsibilities are:

- Music Player decodes its packaged MP3 and submits PCM through `AudioDecoder0`;
- Spectrum Analyser consumes captured PCM published by `AudioEncoder0`;
- volume and mute go through `AudioPlayback`;
- `components/brookesia_service_audio/src/codec_fallback.cpp` bridges these
  services to the native codec HAL while the optional AV processor is disabled.

The browser build's current `brookesia_hal_wasm` audio device is only a mock: it
discards playback PCM, generates silent capture PCM, and stores volume/mute state
without using Web Audio. Do not describe a successful WASM build as proof of real
speaker or microphone support. Real browser audio requires a Web Audio output
bridge and `getUserMedia()` capture implementation below the same Brookesia HAL
interfaces. Keep UI and signal-processing code portable so the native and WASM
apps can ultimately share their application sources.

## WASM browser simulator

The host simulator is supplied by the npm `esp-brookesia-toolkit`. It runs packaged
JavaScript/WASM Brookesia apps in a browser; it does not execute this firmware's
ESP32 native `IApp` classes or emulate the Tab5 ES8388/ES7210 audio hardware.
Use it for JSON GUI, navigation, action, and package testing. Test native HAL,
I2S, codec, microphone, and speaker behavior on the board.

Prerequisites and installation:

```powershell
node --version                 # Node 20 or newer
npm install -g esp-brookesia-toolkit
brookesia doctor
```

`brookesia doctor` should report `node>=20`, `bpk-packager`, and
`wasm_simulator` as available. `usb-cli` and a detected device are not required
for browser simulation.

From a JavaScript app directory containing `package.json`,
`brookesia.config.js`, and a `src` tree:

```powershell
brookesia build
brookesia simulate --package .\dist\my.app.debug.0.1.0.bpk --resolution 720x1280
```

The simulator serves at `http://localhost:8787` by default and normally opens a
browser. For unattended or IDE use:

```powershell
brookesia simulate --package .\dist\my.app.debug.0.1.0.bpk --no-open --port 8787 --pid-file .\simulator.pid
brookesia simulate --stop --pid-file .\simulator.pid
```

Useful simulator checks:

```powershell
brookesia simulate --package .\dist\my.app.debug.0.1.0.bpk --gui-debug
brookesia simulate --package .\dist\my.app.debug.0.1.0.bpk --smoke --duration-ms 1000
```

The toolkit currently installed on this workstation is 1.0.1. Confirm commands
and options after upgrades with `brookesia --version` and
`brookesia simulate --help`.
