# Testing MAP on a XIAO ESP32S3 Sense

Open the `esp32` folder in VS Code and use an ESP-IDF 6.0.1 PowerShell terminal.

```powershell
$env:IDF_TARGET = "esp32s3"
idf.py -B build-map -D "IDF_TARGET=esp32s3" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.map-sense" menuconfig
```

In **ESP32 Device SDK → Muse Gadgets SDK token**, enter your own token. Save
and exit. Keep the generated configuration and firmware private because both
contain the token.

```powershell
idf.py -B build-map build
```

If automatic flashing cannot connect, hold **B**, tap **R**, then release **B**.
Check Device Manager for the COM port. Replace `COM3` below with that port.

```powershell
idf.py -B build-map -p COM3 -b 115200 flash
idf.py -B build-map -p COM3 monitor --no-reset
```

With **B** released, tap **R** and check that `map.bridge: ready` appears and
the board stays up. In the Muse app, enable Developer mode, add
`MuseGadget-MAP…`, and briefly press **B** when prompted. This SDK build uses
Wi-Fi on the ESP32 after Bluetooth pairing.

Ask Muse how MAP is doing to test `pet.status`. The storage startup fix passed
48 host checks and built successfully. It still needs a board boot test.
Automatic task sync needs a reliable way to distinguish new tasks from edits
to existing backend tasks before it is safe to test against shared data.
