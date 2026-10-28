# Firmware application integration verification

This project compiles the actual ESP32-S3 production application, services and
drivers in one isolated ESP-IDF project. It does not modify production main or
production CMake. `MINIMAL_BUILD` avoids unrelated LCD components. No flashing
or serial connection is performed.

Run `./tests/mobile-firmware/build.ps1` from PowerShell. The script uses the
already installed IDF 5.5.5, Python environment and Xtensa toolchain and explicitly
selects `esp32s3`. The component enables `-fstack-usage`.

Run behavior tests with:

```powershell
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-firmware/run_tests.py
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-firmware/run_tests.py --storage
```

The installed ESP RISC-V compiler compiles real C, and the existing deterministic
RV32IM executor runs it. S1, S2, monitor and app/mobile are real production code;
hardware, network, scheduler and screen dependencies are explicit doubles. The
storage suite separately executes actual service/storage with CH376 doubles.
Separate ELF outputs allow independent monitor and storage suites to run together.

`--red` retrieves the original monitor implementation from commit `6153f29` and
demonstrates that external reset was not queued. `--storage --red` uses the same
original storage implementation plus a missing-cache fixture, which fails the
observation assertions. `--app-red` runs a missing-adapter fixture. Red commands
deliberately return exit code 1. Baseline files are generated only under
`build-host`; production files are never reverted by the test runner.

These tests verify owner execution, queue capacity eight, execution deadlines,
idempotent start/pause, reset epochs, effective elapsed time beyond 24 hours,
pause freeze, synchronization independence, stable snapshot identity, startup
failure and permanent completion callbacks outside snapshot locks. A started
storage transaction may finish past its deadline; the next queued request is
then checked independently and expires before execution. Storage assertions
cover no-I/O reads, explicit disconnect, failed mount, file I/O failure and
independent current-operation absence evidence.

Generated `build`, `build-host`, `sdkconfig`, `sdkconfig.old` and lock/cache files
are not source deliverables. Build and verification evidence is in
`reports/mobile-app-v1/firmware`. Hardware execution, phone pairing, storage
insertion/removal on a board, and RTOS stack high-water measurements are NOT_RUN.
