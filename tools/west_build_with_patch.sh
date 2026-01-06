#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIDEWALK_PATCH="${SIDEWALK_PATCH:-$ROOT_DIR/app/evse_interlock_v1/patches/sidewalk-ble-off.patch}"

if [[ ! -f "${SIDEWALK_PATCH}" ]]; then
  echo "WARN: sidewalk patch not found: ${SIDEWALK_PATCH}" >&2
else
  if git -C "${ROOT_DIR}/sidewalk" apply --reverse --check "${SIDEWALK_PATCH}" >/dev/null 2>&1; then
    echo "INFO: sidewalk patch already applied"
  else
    echo "INFO: applying sidewalk patch: ${SIDEWALK_PATCH}"
    git -C "${ROOT_DIR}/sidewalk" apply "${SIDEWALK_PATCH}"
  fi
fi

if [[ $# -eq 0 ]]; then
  echo "Usage: ${SCRIPT_NAME} <west build args...>" >&2
  echo "Example: ${SCRIPT_NAME} -p always -d build -b rak4631 app/evse_interlock_v1 -- -DOVERLAY_CONFIG=config/overlays/overlay-sidewalk_logging_v1.conf -DDTC_OVERLAY_FILE=config/overlays/rak4631.overlay" >&2
  exit 2
fi

west build "$@"
