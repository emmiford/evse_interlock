# CODEX_HANDOFF

1) Current repo root and board target
- Repo root: `/Users/jan/dev/sidewalk-workspace`
- Board target: `rak4631` (qualifier `nrf52840`)
- Found in: `build/CMakeCache.txt` and `build/build_info.yml`

Docs index:
- Setup: `docs/SETUP.md`
- Testing: `docs/TESTING.md`
- Architecture: `docs/ARCHITECTURE.md`

2) Exact build + flash commands that work
- Build:
  - `tools/west_build_with_patch.sh -p always -d /Users/jan/dev/sidewalk-workspace/build -b rak4631 /Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1 -- -DOVERLAY_CONFIG=config/overlays/overlay-sidewalk_logging_v1.conf -DDTC_OVERLAY_FILE=config/overlays/rak4631.overlay -DPM_STATIC_YML_FILE:FILEPATH=/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/config/pm_static_rak4631_nrf52840.yml -Dmcuboot_PM_STATIC_YML_FILE:FILEPATH=/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/config/pm_static_rak4631_nrf52840.yml`
- Flash app:
  - `west flash --runner pyocd --build-dir /Users/jan/dev/sidewalk-workspace/build -- --target nrf52840 --dev-id 0700000100120036470000124e544634a5a5a5a597969908`
- Flash mfg (after provisioning):
  - `pyocd flash -t nrf52840 -u 0700000100120036470000124e544634a5a5a5a597969908 /Users/jan/dev/sidewalk-workspace/sidewalk/tools/provision/rak4631_mfg.hex`
- RTT:
  - `pyocd rtt -t nrf52840 -u 0700000100120036470000124e544634a5a5a5a597969908`

3) Sidewalk configuration details (versions, key settings)
- Versions (from device log):
  - Sidewalk SDK: `1.19.4.20`
  - App build: `v1.1.0-add-on`
  - NCS: `v3.0.0-3bfc46578e42`
  - Zephyr: `v4.0.99-ncs1`
- Board: `rak4631/nrf52840`
- Shield: `Semtech sx1262`
- Manufacturing/provisioning:
  - `.secrets/sidewalk/certificate.json` (ignored in git)
  - MFG address: `0xFC000`
  - Provision command (example):
    - `python3 provision.py nordic aws --certificate_json /Users/jan/dev/sidewalk-workspace/.secrets/sidewalk/certificate.json --addr 0xFC000 --output_bin mfg.bin --output_hex rak4631_mfg.hex`
- Key app configs (hello):
  - `app/evse_interlock_v1/config/overlays/overlay-sidewalk_logging_v1.conf`
  - `CONFIG_SID_END_DEVICE_HELLO=y`
  - `CONFIG_SID_END_DEVICE_GPIO_EVENTS=y`
  - `CONFIG_SID_END_DEVICE_GPIO_SIMULATOR=y`
  - BLE is disabled in `app/evse_interlock_v1/prj.conf`
  - Sidewalk BLE-off patch is applied at build time:
    - `app/evse_interlock_v1/patches/sidewalk-ble-off.patch`
    - `tools/west_build_with_patch.sh`
    - `tools/sidewalk_build_flash.sh`

4) Where the GPIO input code lives and which alias/pin it uses
- Code: `app/evse_interlock_v1/src/main/app.c`
- Pure logic module:
  - `app/evse_interlock_v1/src/telemetry/gpio_event.h`
  - `app/evse_interlock_v1/src/telemetry/gpio_event.c`
- Alias: `extinput0`
- Alias definition: `app/evse_interlock_v1/config/overlays/rak4631.overlay`
- Pin: `GPIO0.11` (active low, pull-up)

5) How uplinks are sent (function names, files)
- Send path:
  - `app/evse_interlock_v1/src/main/app.c` -> `sidewalk_event_send(sidewalk_event_send_msg, ...)`
  - Handler: `app/evse_interlock_v1/src/sidewalk_events.c` (`sidewalk_event_send_msg`)
- Payload build:
  - `gpio_event_build_payload()` in `gpio_event.c`
- Log on success/error:
  - `Sidewalk send: ok 0`
  - `Sidewalk send: err <code>`

6) How AWS receives messages (destination/rule/log group/topic)
- Region: `us-east-1`
- Destination: `SensorAppDestination`
- IoT Core topic: `sidewalk/app_data`
- E2E verification uses MQTT subscribe to `sidewalk/app_data` via `awsiotsdk`:
  - `tests/mqtt_wait_for_run_id.py`

7) Test plan status: implemented vs pending
- Implemented:
  - Unit tests (host): `app/evse_interlock_v1/tests/telemetry/host`
  - HIL test: `tests/test_hil_gpio.sh` + `tests/test_hil_gpio.py`
  - E2E test: `tests/test_e2e_sidewalk.sh`
  - Test plan doc: `docs/TESTING.md`
- Pending:
  - Run unit/HIL/E2E tests to confirm in this environment.
  - Optional: switch from simulator to real GPIO input wiring.

8) Known issues / gotchas
- RAK4631 DTS has no readable user button (`gpio-keys`); reset is nRESET only.
- Simulator is enabled by default; disable with:
  - `CONFIG_SID_END_DEVICE_GPIO_SIMULATOR=n` in `config/overlays/overlay-sidewalk_logging_v1.conf`.
- `awsiotsdk` required for E2E MQTT subscribe:
  - `python3 -m pip install awsiotsdk`
- `tests/test_zephyr_linux.sh` only runs on Linux (native_posix).

9) Files created/edited/deleted during this session
- Created:
  - `app/evse_interlock_v1/src/telemetry/gpio_event.c`
  - `app/evse_interlock_v1/src/telemetry/gpio_event.h`
  - `app/evse_interlock_v1/config/overlays/rak4631.overlay`
  - `app/evse_interlock_v1/tests/telemetry/gpio_event/CMakeLists.txt`
  - `app/evse_interlock_v1/tests/telemetry/gpio_event/prj.conf`
  - `app/evse_interlock_v1/tests/telemetry/gpio_event/src/main.c`
  - `docs/TESTING.md`
  - `tests/test_unit_host.sh`
  - `tests/test_hil_gpio.sh`
  - `tests/test_hil_gpio.py`
  - `tests/capture_rtt_run_id.py`
  - `tests/mqtt_wait_for_run_id.py`
  - `tests/test_e2e_sidewalk.sh`
- Edited:
  - `app/evse_interlock_v1/src/main/app.c`
  - `app/evse_interlock_v1/Kconfig`
  - `app/evse_interlock_v1/config/overlays/overlay-sidewalk_logging_v1.conf`
  - `app/evse_interlock_v1/CMakeLists.txt`
  - `sidewalk/tools/provision/.gitignore` (added `keys/`)
  - `docs/SETUP.md`
  - `tools/sidewalk_build_flash.sh`
- Deleted:
  - `app/evse_interlock_v1/src/test_mode/app.c` (DUT variant removed)
  - CLI sources in `app/evse_interlock_v1/src/test_mode/` (shell/CLI removed)

---
## Change log (detailed)

### Files changed/created and why
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/src/main/app.c`
  - Added GPIO change detection, debounce scheduling, simulator/test mode, run_id logging, and Sidewalk uplink send on state changes.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/src/telemetry/gpio_event.h`
  - New pure-logic interface for debounce/edge detection and payload building (unit-testable).
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/src/telemetry/gpio_event.c`
  - New pure-logic implementation for debounce/edge detection and payload formatting.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/Kconfig`
  - Added GPIO test configs: debounce/poll intervals, simulator, and E2E test mode.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/CMakeLists.txt`
  - Added `src/telemetry/gpio_event.c` to build.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/overlays/overlay-sidewalk_logging_v1.conf`
  - Enabled GPIO events/simulator config for the hello variant.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/overlays/rak4631.overlay`
  - New `extinput0` GPIO alias for RAK4631 on GPIO0.11 (active low, pull-up).
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/tests/telemetry/gpio_event/*`
  - New ztest unit tests for debounce/edge/payload behavior (host-native).
- `/Users/jan/dev/sidewalk-workspace/docs/TESTING.md`
  - Test plan and E2E trigger documentation.
- `/Users/jan/dev/sidewalk-workspace/docs/SETUP.md`
  - Consolidated setup/provisioning details.
- `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/config/pm_static_rak4631_nrf52840.yml`
  - Static partition layout for RAK4631 build (created earlier in session).
- `/Users/jan/dev/sidewalk-workspace/tools/sidewalk_build_flash.sh`
  - Updated build/flash/provision flow and probe handling (created/edited earlier).
- `/Users/jan/dev/sidewalk-workspace/tests/test_unit_host.sh`
  - Host unit-test runner (west native_posix).
- `/Users/jan/dev/sidewalk-workspace/tests/test_hil_gpio.sh`
  - HIL build/flash + RTT assert runner.
- `/Users/jan/dev/sidewalk-workspace/tests/test_hil_gpio.py`
  - Parses RTT output for GPIO event + send ok.
- `/Users/jan/dev/sidewalk-workspace/tests/capture_rtt_run_id.py`
  - Captures `run_id` from RTT log for E2E.
- `/Users/jan/dev/sidewalk-workspace/tests/mqtt_wait_for_run_id.py`
  - MQTT (awsiotsdk) subscriber to confirm AWS receipt of `run_id`.
- `/Users/jan/dev/sidewalk-workspace/tests/test_e2e_sidewalk.sh`
  - Automated E2E run: build/flash, capture run_id, verify AWS receipt.
- `/Users/jan/dev/sidewalk-workspace/CODEX_HANDOFF.md`
  - This handoff document.

### Sidewalk submodule state
- Submodule is kept clean; BLE changes are applied at build time from:
  - `/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/patches/sidewalk-ble-off.patch`

### Key snippets
Payload format (from `gpio_event_build_payload`):
```
{"source":"gpio","pin":"extinput0","state":<0/1>,"edge":"rising|falling","uptime_ms":<ms>,"run_id":"<id>"}
```
Run ID logging (from `app.c`):
```
LOG_INF("E2E run_id: %s", app_gpio_run_id);
```
GPIO event logging:
```
LOG_INF("GPIO event: %s state=%d edge=%s uptime_ms=%" PRId64, ...);
```
