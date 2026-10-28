# mobile_link host checks

These tests compile the real production `mobile_link.c` with independent HTTPD,
FreeRTOS and socket doubles. The parent-owned `../rv32_host.py` executes the
freestanding RV32IM C binary using the already installed ESP GCC and IDF Python.
No board, serial port, flashing, GUI or added packages are used.

From the `mobile-firmware` worktree:

```powershell
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-ap-smoke/mobile_link-tests/run_tests.py
```

Expected: exit 0, `PASS mobile_link: all lifecycle, frame, token, queue and callback assertions`.

The retained API-only missing-implementation fixture reproduces the original RED:

```powershell
& 'C:/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe' tests/mobile-ap-smoke/mobile_link-tests/run_tests.py --red
```

Expected: exit 1, first `ERR_PARAM` assertion fails. This option never changes
production code. An additional RED was observed when the handshake double was
corrected to match IDF 5.5.5: `on_open` was absent because successful upgrades
skip the GET URI handler. Production now uses `ws_post_handshake_cb`.

Assertions cover configuration validation; allocation/start/registration/queue
failures; stop retry during both rollback and normal shutdown; callback context
and lock state; callback send and callback stop no-op; handshake-time rejection
of a second session; 1024-byte acceptance and 1025-byte rejection before payload
read; payload ownership; stale tokens after fd reuse and after restart; queued
work dropping stale generations or a non-WebSocket fd; close-then-send; four-job
backpressure; work executing before enqueue returns; send/receive failures;
binary/fragmented rejection; empty text; and reclamation when HTTPD discards
queued work on shutdown. The doubles detect double-free and nested locking.
Send failure additionally verifies owner-task `shutdown(fd, SHUT_RDWR)`, no SDK
asynchronous bare-session close, rejection of subsequent/queued sends, and a
single `on_close` only when HTTPD's normal close callback subsequently executes.
The lwIP stub also mirrors its signed-byte `err_t` typedef and `ERR_OK` enum;
the private SDK include boundary must load/rename these before BSP's public
int32_t `err_t` and error macros. This regression was found by the real SDK build
and was absent from the original simplified stubs.

Required SDK flags are `CONFIG_HTTPD_WS_SUPPORT`,
`CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT`, and
`CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT`. A single external task serializes
start/stop. Shutdown retries SDK errors without releasing callback context;
permanent SDK failure blocks rather than returning an unsafe lifetime guarantee.

Limits: the executor checks C behavior and explicit interleavings, not real
FreeRTOS multicore scheduling, HTTP bytes, lwIP behavior, or Wi-Fi. Masked frame
decoding and ping/pong/close handling belong to ESP-IDF HTTPD. Real ESP32-S3
connection, hello/echo, close/reconnect and stress verification remain hardware
acceptance work. No business JSON is parsed by this driver.
