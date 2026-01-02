#!/usr/bin/env python3
import csv
import json
import os
import sys
from pathlib import Path

import boto3


def _import_site_id_helpers():
    tools_dir = Path(__file__).resolve().parent
    sys.path.insert(0, str(tools_dir))
    from site_id_from_address import (  # type: ignore
        normalize_canonical_address,
        site_id_encrypt,
    )

    return normalize_canonical_address, site_id_encrypt


def validate_sidewalk_sn(sn: str) -> bool:
    return len(sn) == 64 and all(c in "0123456789abcdefABCDEF" for c in sn)


def log(msg: str, output_file: Path) -> None:
    with output_file.open("a", encoding="utf-8") as f:
        f.write(msg + "\n")


def main() -> None:
    region = os.getenv("AWS_REGION", "us-east-1")
    project_prefix = os.getenv("PROJECT_PREFIX", "sidewalk-v1")
    csv_path = Path(os.getenv("CSV_PATH", "tools/devices_batch.csv"))
    key_file = Path(os.getenv("SITE_ID_KEY_FILE", "tools/site_id_key.b64"))
    output_file = Path(os.getenv("OUTPUT_FILE", "aws_setup_output.txt"))
    customer_id = os.getenv("CUSTOMER_ID", "default")

    output_file.write_text("", encoding="utf-8")

    if not key_file.exists():
        raise SystemExit(f"Missing siteId key file: {key_file}")
    key_b64 = key_file.read_text(encoding="utf-8").strip()

    normalize_canonical_address, site_id_encrypt = _import_site_id_helpers()
    ddb = boto3.resource("dynamodb", region_name=region)
    config_table = ddb.Table(f"{project_prefix}-device_config")

    with csv_path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    required_cols = {"address", "sidewalk_sn"}
    if not rows or not required_cols.issubset(rows[0].keys()):
        raise SystemExit(f"CSV must include: {', '.join(sorted(required_cols))}")

    for idx, row in enumerate(rows, start=1):
        address = (row.get("address") or "").strip()
        sn = (row.get("sidewalk_sn") or "").strip()
        cert_path = (row.get("certificate_json_path") or "").strip()
        device_type = (row.get("device_type") or "evse").strip()
        rated_voltage_v = row.get("rated_voltage_v") or "240"
        rated_power_heating_kw = row.get("rated_power_heating_kw") or "0"
        rated_power_cooling_kw = row.get("rated_power_cooling_kw") or "0"

        if not address:
            log(f"Row {idx}: missing address; skipping.", output_file)
            continue
        if not sn and cert_path:
            try:
                obj = json.loads(Path(cert_path).read_text(encoding="utf-8"))
                sn = (obj.get("metadata") or {}).get("smsn", "").strip()
            except Exception:
                sn = ""

        if not validate_sidewalk_sn(sn):
            log(f"Row {idx}: invalid sidewalk_sn; skipping.", output_file)
            continue

        standardized = normalize_canonical_address(address)
        if standardized == "INVALID_FORMAT" or not standardized:
            log(f"Row {idx}: invalid address format; skipping.", output_file)
            continue

        site_id_full = site_id_encrypt(standardized, customer_id, key_b64)
        site_id_short = site_id_full[:8]

        # Resolve device_id by SidewalkManufacturingSn
        client = boto3.client("iotwireless", region_name=region)
        resp = client.list_wireless_devices()
        device_id = None
        for dev in resp.get("WirelessDeviceList", []):
            sw = dev.get("Sidewalk") or {}
            if sw.get("SidewalkManufacturingSn") == sn:
                device_id = dev.get("Id")
                break
        if not device_id:
            log(f"Row {idx}: device_id not found for sidewalk_sn; skipping.", output_file)
            continue

        config_table.put_item(
            Item={
                "device_id": device_id,
                "device_type": device_type,
                "site_id": site_id_short,
                "rated_voltage_v": int(float(rated_voltage_v)),
                "rated_power_heating_kw": float(rated_power_heating_kw),
                "rated_power_cooling_kw": float(rated_power_cooling_kw),
            }
        )

        log(f"Row {idx}: seeded device_id={device_id}", output_file)


if __name__ == "__main__":
    main()
