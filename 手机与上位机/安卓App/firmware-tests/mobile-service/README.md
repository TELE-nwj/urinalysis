# Mobile service C tests

The tests compile the real `second sol/main/service/mobile/mobile.c` with the
installed ESP GCC (`-Wall -Wextra -Werror`) and execute the resulting RV32IM
machine code using the existing `tests/mobile-ap-smoke/rv32_host.py` CPU runner.
FreeRTOS, WiFi and WebSocket boundaries are explicit SDK doubles. No hardware,
radio, operating-system scheduler, HTTP server or physical monitor is simulated.
The printf double supports the production service's format subset; production
newlib formatting and FreeRTOS ABI require the integrated ESP-IDF build.

From the firmware worktree:

```powershell
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-service/run_tests.py
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-service/run_tests.py --red
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-service/run_tests.py --race-red
```

The normal invocation must pass. `--red` uses an unimplemented service fixture
and must fail the first start assertion. `--race-red` compiles a temporary copy
with ACK cache refresh disabled and must fail the wire phase monotonicity test.
Neither mode changes the production source.

The suite covers strict JSON grammar/types, escaped duplicate keys, req_id
bounds, unknown commands/fields, HMS bounds, all admission failure mappings,
accepted/completed behavior, terminal idempotence, semantic request identity,
32-entry eviction, old peer/completion/session rejection, five-second admission
deadlines, 1 Hz status scheduling, state-triggered sends, unchanged seq/timestamp
on repeated snapshots, invalid provider snapshots, nullable fields, 64-bit
counters, status send backpressure, ACK interleaving, hello retry/order,
lifecycle failure ownership and idempotent stop. Snapshot/submit/network doubles
also assert that the service data mutex is never held across these calls.

The ACK interleaving test invokes completion precisely after the accepted reply
is copied and the service data mutex is released. The hello interleaving test
invokes tick after on_open publishes the session and releases that mutex.
These are deterministic control-flow regressions; they do not claim a real
FreeRTOS concurrency stress test.
