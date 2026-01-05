#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

WORKSPACE="${WORKSPACE:-$HOME/dev/sidewalk-workspace}"
BUILD_DIR="${BUILD_DIR:-$WORKSPACE/build}"
OVERLAY_CONFIG="${OVERLAY_CONFIG:-config/overlays/overlay-sidewalk_logging_v1.conf}"
BOARD="${BOARD:-rak4631}"
MFG_ADDR="${MFG_ADDR:-0xFC000}"
PROBE_ID="${PROBE_ID:-0700000100120036470000124e544634a5a5a5a597969908}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/opt/zephyr-sdk-0.16.0}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"

PM_STATIC_YML="${PM_STATIC_YML:-$WORKSPACE/app/evse_interlock_v1/config/pm_static/pm_static_rak4631_nrf52840.yml}"
PROVISION_DIR="${PROVISION_DIR:-$WORKSPACE/sidewalk/tools/provision}"
CERT_JSON="${CERT_JSON:-$WORKSPACE/.secrets/sidewalk/certificate.json}"

if [[ ! -f "$PM_STATIC_YML" ]]; then
  echo "Missing PM static file: $PM_STATIC_YML" >&2
  exit 1
fi

if [[ ! -f "$CERT_JSON" ]]; then
  echo "Missing certificate.json: $CERT_JSON" >&2
  exit 1
fi

probe_args=()
if [[ -n "$PROBE_ID" ]]; then
  probe_args=(--dev-id "$PROBE_ID")
fi
pyocd_probe_args=()
if [[ -n "$PROBE_ID" ]]; then
  pyocd_probe_args=(-u "$PROBE_ID")
fi

echo "=== Build ==="
pushd "$WORKSPACE" >/dev/null
west build -p always -d "$BUILD_DIR" -b "$BOARD" "$WORKSPACE/app/evse_interlock_v1" -- \
  -DOVERLAY_CONFIG="$OVERLAY_CONFIG" \
  -DPM_STATIC_YML_FILE:FILEPATH="$PM_STATIC_YML"
popd >/dev/null

echo "=== Flash app (merged.hex) ==="
west flash --runner pyocd --build-dir "$BUILD_DIR" -- \
  --target nrf52840 ${probe_args[@]:-}

echo "=== Provision (mfg) ==="
pushd "$PROVISION_DIR" >/dev/null
pip install -r requirements.txt >/dev/null
python3 provision.py nordic aws \
  --certificate_json "$CERT_JSON" \
  --addr "$MFG_ADDR" \
  --output_bin mfg.bin \
  --output_hex rak4631_mfg.hex
popd >/dev/null

echo "=== Flash mfg hex ==="
pyocd flash -t nrf52840 ${pyocd_probe_args[@]:-} "$PROVISION_DIR/rak4631_mfg.hex"

echo "=== Done ==="
echo "RTT: pyocd rtt -t nrf52840 ${pyocd_probe_args[*]:-}"
echo "PASS: ${SCRIPT_NAME}"
