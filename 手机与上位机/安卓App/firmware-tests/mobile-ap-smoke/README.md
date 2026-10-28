# F0 mobile AP connection trial

This isolated ESP32-S3 project compiles the reusable WiFi and WebSocket drivers
directly. Its `app_main` sends a **SIMULATED** hello and echoes text unchanged.
It never initializes a sensor, CH376, buttons, serial screen or measurement.
It is not a measurement firmware and does not implement protocol-v1 commands.

Trial WiFi: `NIAIO-F0-SIMULATED`, password `niaio-f0-test`, channel 6.
WebSocket: `ws://192.168.4.1/ws` (port 80). Four WiFi associations are permitted;
only one WebSocket session is admitted. The second upgrade receives HTTP 503.

First received text:

```json
{"type":"hello","mode":"SIMULATED","protocol":"mobile-ap-smoke-v1"}
```

Text is echoed byte for byte. Maximum frame size is 1024 bytes; binary,
continuation and fragmented text messages are rejected. Queued delivery is
bounded to four frames. `ERR_OK` from send means queued, not delivered.

Build from the firmware worktree with the existing SDK/toolchain only:

```powershell
& ./tests/mobile-ap-smoke/build.ps1
```

This command only builds. ESP32-S3 and all three HTTPD WebSocket feature flags
are set by this isolated project's CMake/defaults. Production CMake, main and
sdkconfig are not modified. Do not run the repository's `flash.bat` for this
trial: it also flashes a different project.

Run deterministic C behavioral tests without hardware:

```powershell
$python = 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe'
& $python tests/mobile-ap-smoke/wifi-tests/run_tests.py --red # expected exit 1
& $python tests/mobile-ap-smoke/wifi-tests/run_tests.py
& $python tests/mobile-ap-smoke/mobile_link-tests/run_tests.py --red # expected exit 1
& $python tests/mobile-ap-smoke/mobile_link-tests/run_tests.py
```

The real driver sources are compiled to RV32IM using the installed IDF GCC,
then executed in `rv32_host.py` with narrow SDK doubles. These tests check C
control flow, resource ownership, frame limits and token lifetimes. They do not
simulate scheduling, the ESP32 radio, DHCP, lwIP or the real HTTPD socket stack.

Hardware connection validation remains separate. Once a prepared device runs
this trial, join its WiFi, keep the connection when the phone reports no
Internet, and run the Android E0 client or this standard-library Python client
from a computer joined to the same AP:

```powershell
& $python tests/mobile-ap-smoke/smoke_client.py
```

Expected output is `PASS SIMULATED hello; exact UTF-8 echo; second WebSocket
rejected`. Also manually verify reconnect after app closure and AP loss. The
current F0 implementation task neither flashes a board nor opens a serial
port; successful compilation and C tests do not establish hardware acceptance.
