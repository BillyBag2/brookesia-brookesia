# ESP-IDF commands

## Read the connected chip revision

Run these commands in an ESP-IDF PowerShell terminal. For this workspace's
configured ESP-IDF installation, initialize a regular PowerShell terminal with:

```powershell
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.1\venv'
& 'C:\esp\v6.1\esp-idf\export.ps1'
```

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
