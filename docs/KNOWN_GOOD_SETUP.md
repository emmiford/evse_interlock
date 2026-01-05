# Known-Good Setup (RAK4630 / nRF52840 Sidewalk)

This note captures a working build + flash flow with explicit versions and
paths used on macOS (Intel) with the RAK4630.

## Versions

- nRF Connect SDK: v3.0.0-3bfc46578e42
- Zephyr: v4.0.99-ncs1 (from NCS v3.0.0)
- Sidewalk add-on: v0.1.99-addon-ff9b3a40843a
- Sidewalk SDK: 1.19.4.20
- Zephyr SDK: 0.16.0

## Workspace Folder to Open in VS Code

- `/Users/jan/dev/sidewalk-workspace`

## Required Files

- PM static file: `app/evse_interlock_v1/config/pm_static/pm_static_rak4631_nrf52840.yml`
- Provisioning JSON: `.secrets/sidewalk/certificate.json`

## Build + Flash (CLI)

```bash
rm -rf /Users/jan/dev/sidewalk-workspace/build

west build -b rak4631 /Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1 -- \
  -DOVERLAY_CONFIG="config/overlays/overlay-sidewalk_logging_v1.conf" \
  -DPM_STATIC_YML_FILE:FILEPATH=/Users/jan/dev/sidewalk-workspace/app/evse_interlock_v1/config/pm_static/pm_static_rak4631_nrf52840.yml

west flash --runner pyocd --build-dir /Users/jan/dev/sidewalk-workspace/build -- \
  --target nrf52840 --dev-id 0700000100120036470000124e544634a5a5a5a597969908
```

## Provisioning (mfg)

```bash
cd /Users/jan/dev/sidewalk-workspace/sidewalk/tools/provision
pip install -r requirements.txt

python3 provision.py nordic aws \
  --certificate_json /Users/jan/dev/sidewalk-workspace/.secrets/sidewalk/certificate.json \
  --addr 0xFC000 \
  --output_bin mfg.bin \
  --output_hex rak4631_mfg.hex

pyocd flash -t nrf52840 -u 0700000100120036470000124e544634a5a5a5a597969908 \
  rak4631_mfg.hex
```

## Verify Boot Logs (RTT)

```bash
pyocd rtt -t nrf52840 -u 0700000100120036470000124e544634a5a5a5a597969908
```

Expected log lines after provisioning:

- `sid_mfg: Successfully parsed mfg data`
- `Device Is registered, Time Sync Success, Link status: {BLE: Up, ...}`

## Troubleshooting

- `PM_MCUBOOT_SECONDARY_ID` missing or `mfg_storage` undefined
  - Cause: static PM file not applied. Fix by using the PM file above and a pristine build.
- `mfg.hex version mismatch`
  - Cause: mfg data not provisioned. Fix by running `provision.py` and flashing `rak4631_mfg.hex` to 0xFC000.
- No USB serial devices
  - Use RTT over DAP (`pyocd rtt`) instead of UART.
