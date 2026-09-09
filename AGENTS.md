# ESP-IDF commands

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
  ES8388/ES7210 codec interfaces directly while that incompatibility remains.

For a faster rebuild, ccache can be kept inside this workspace:

```powershell
$env:CCACHE_DIR = "$PWD\.ccache"
$env:CCACHE_TEMPDIR = "$PWD\.ccache\tmp"
idf.py build
```

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
