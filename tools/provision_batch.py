#!/usr/bin/env python3
import argparse
import csv
import json
import os
import subprocess
import sys
import time
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
DEFAULT_CSV = TOOLS_DIR / "devices_batch.csv"
DEFAULT_KEY_FILE = TOOLS_DIR / "site_id_key.b64"
DEFAULT_OUTPUT_FILE = Path("bash_output.txt")


def _import_site_id_helpers():
    sys.path.insert(0, str(TOOLS_DIR))
    from site_id_from_address import (  # type: ignore
        generate_key_b64,
        normalize_canonical_address,
        site_id_encrypt,
    )

    return generate_key_b64, normalize_canonical_address, site_id_encrypt


def log(msg: str, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("a", encoding="utf-8") as f:
        f.write(msg + "\n")
    print(msg)


def run(cmd, output_path: Path, check=True, dry_run=False) -> str:
    log(f"$ {' '.join(cmd)}", output_path)
    if dry_run:
        return ""
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout:
        log(proc.stdout.rstrip(), output_path)
    if proc.stderr:
        log(proc.stderr.rstrip(), output_path)
    if check and proc.returncode != 0:
        raise RuntimeError(f"Command failed: {' '.join(cmd)}")
    return proc.stdout.strip()


def ensure_key_file(key_file: Path, output_path: Path) -> str:
    generate_key_b64, _, _ = _import_site_id_helpers()
    if key_file.exists():
        return key_file.read_text(encoding="utf-8").strip()
    key = generate_key_b64()
    key_file.write_text(key + "\n", encoding="utf-8")
    log(f"Wrote siteId key to {key_file}", output_path)
    return key


def parse_zip_from_canonical(canonical: str) -> str:
    # Canonical format: "STREET CITY ST ZIP" (ZIP or ZIP+4)
    parts = canonical.split()
    if not parts:
        return ""
    return parts[-1]


def short_serial_from_sn(sn: str) -> str:
    sn = sn.strip()
    return sn[-8:] if len(sn) >= 8 else sn


def validate_sidewalk_sn(sn: str) -> bool:
    return len(sn) == 64 and all(c in "0123456789abcdefABCDEF" for c in sn)


def smsn_from_certificate_json(path: str) -> str:
    try:
        obj = json.loads(Path(path).read_text(encoding="utf-8"))
    except Exception:
        return ""
    meta = obj.get("metadata") or {}
    return (meta.get("smsn") or "").strip()


def detect_probe_id(output_path: Path) -> str:
    # Prefer JSON if available to avoid parsing text.
    try:
        out = run(["pyocd", "list", "--json"], output_path, check=False)
        if out:
            data = json.loads(out)
            probes = [p.get("unique_id") for p in data if p.get("unique_id")]
            probes = [p for p in probes if p]
            if len(probes) == 1:
                return probes[0]
            if len(probes) > 1:
                log("Multiple probes detected; please enter the probe ID.", output_path)
    except Exception:
        pass

    probe = input("Probe ID (pyocd --dev-id): ").strip()
    if not probe:
        raise RuntimeError("Probe ID is required to flash.")
    return probe


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Batch provision Sidewalk devices from a CSV."
    )
    parser.add_argument(
        "--csv",
        default=str(DEFAULT_CSV),
        help="CSV path with at least an 'address' column.",
    )
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT_FILE),
        help="Log output file path.",
    )
    parser.add_argument(
        "--key-file",
        default=str(DEFAULT_KEY_FILE),
        help="siteId encryption key file (auto-generated if missing).",
    )
    parser.add_argument(
        "--region",
        default=os.getenv("AWS_REGION", "us-east-1"),
        help="AWS region.",
    )
    parser.add_argument(
        "--device-profile-id",
        default="d01c298b-0a71-4ec6-9cb6-33d8ae2286b9",
        help="Sidewalk device profile ID.",
    )
    parser.add_argument(
        "--destination-name",
        default="SensorAppDestination",
        help="Sidewalk destination name for uplinks.",
    )
    parser.add_argument(
        "--hardware-rev",
        default="RAK4630",
        help="Hardware revision tag.",
    )
    parser.add_argument(
        "--firmware-rev",
        default="v1.1.0-add-on",
        help="Firmware revision tag.",
    )
    parser.add_argument(
        "--device-type",
        default="EVSE",
        help="Device type tag (e.g., EVSE).",
    )
    parser.add_argument(
        "--customer-id",
        default="default",
        help="Customer ID tag.",
    )
    parser.add_argument(
        "--build-dir",
        default=str(Path("build").resolve()),
        help="West build directory for flashing app.",
    )
    parser.add_argument(
        "--skip-app-flash",
        action="store_true",
        help="Skip flashing the application image.",
    )
    parser.add_argument(
        "--skip-aws-create",
        action="store_true",
        help="Skip AWS create-wireless-device (for dry runs).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Log actions without calling AWS or flashing devices.",
    )
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.write_text("", encoding="utf-8")

    generate_key_b64, normalize_canonical_address, site_id_encrypt = _import_site_id_helpers()
    key_b64 = ensure_key_file(Path(args.key_file), output_path)

    csv_path = Path(args.csv)
    if not csv_path.exists():
        raise SystemExit(f"Missing CSV file: {csv_path}")

    provision_dir = Path("sidewalk/tools/provision").resolve()
    device_profile_json = provision_dir / "device_profile.json"

    if not device_profile_json.exists() and not args.skip_aws_create and not args.dry_run:
        run(
            [
                "aws",
                "--region",
                args.region,
                "iotwireless",
                "get-device-profile",
                "--id",
                args.device_profile_id,
            ],
            output_path,
            check=True,
        )
        device_profile_json.write_text(
            run(
                [
                    "aws",
                    "--region",
                    args.region,
                    "iotwireless",
                    "get-device-profile",
                    "--id",
                    args.device_profile_id,
                    "--output",
                    "json",
                ],
                output_path,
                check=True,
            )
            + "\n",
            encoding="utf-8",
        )

    with csv_path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    if not rows or "address" not in rows[0]:
        raise SystemExit("CSV must include an 'address' column.")

    for idx, row in enumerate(rows, start=1):
        address = (row.get("address") or "").strip()
        sidewalk_sn = (row.get("sidewalk_sn") or "").strip()
        cert_path = (row.get("certificate_json_path") or "").strip()
        if not address:
            log(f"Row {idx}: missing address; skipping.", output_path)
            continue

        standardized = normalize_canonical_address(address)
        if standardized == "INVALID_FORMAT" or not standardized:
            log(
                f"Row {idx}: invalid address format. Use: 'street, city, ST ZIP'.",
                output_path,
            )
            continue

        canonical = standardized
        site_id_full = site_id_encrypt(canonical, args.customer_id, key_b64)
        site_id_short = site_id_full[:8]
        install_zip = parse_zip_from_canonical(canonical)

        if not sidewalk_sn and cert_path:
            sidewalk_sn = smsn_from_certificate_json(cert_path)

        if not sidewalk_sn:
            log(
                f"Row {idx}: missing sidewalk_sn; cannot create AWS device or mfg hex.",
                output_path,
            )
            continue
        if not validate_sidewalk_sn(sidewalk_sn):
            log(
                f"Row {idx}: sidewalk_sn must be 64 hex chars; skipping.",
                output_path,
            )
            continue

        short_serial = short_serial_from_sn(sidewalk_sn)
        device_name = f"sw-rak4630-{site_id_short}-{short_serial}"

        log(f"Row {idx}: device_name={device_name}", output_path)

        if not args.skip_aws_create and not args.dry_run:
            sidewalk_arg = (
                f"DeviceProfileId={args.device_profile_id},"
                f"SidewalkManufacturingSn={sidewalk_sn}"
            )
            run(
                [
                    "aws",
                    "--region",
                    args.region,
                    "iotwireless",
                    "create-wireless-device",
                    "--type",
                    "Sidewalk",
                    "--name",
                    device_name,
                    "--destination-name",
                    args.destination_name,
                    "--sidewalk",
                    sidewalk_arg,
                    "--tags",
                    f"Key=customerId,Value={args.customer_id}",
                    f"Key=siteId,Value={site_id_short}",
                    f"Key=installZip,Value={install_zip}",
                    f"Key=installAddrHash,Value={site_id_full}",
                    f"Key=hardwareRev,Value={args.hardware_rev}",
                    f"Key=firmwareRev,Value={args.firmware_rev}",
                    f"Key=deviceType,Value={args.device_type}",
                ],
                output_path,
                check=True,
            )

        wireless_id = ""
        if not args.dry_run:
            # Fetch wireless device JSON by manufacturing SN (best-known unique)
            wireless_id = run(
                [
                    "aws",
                    "--region",
                    args.region,
                    "iotwireless",
                    "list-wireless-devices",
                    "--query",
                    f"WirelessDeviceList[?Sidewalk.SidewalkManufacturingSn=='{sidewalk_sn}'].Id | [0]",
                    "--output",
                    "text",
                ],
                output_path,
                check=True,
            )
            if not wireless_id or wireless_id == "None":
                raise RuntimeError(f"Row {idx}: unable to resolve wireless device ID.")

        out_dir = provision_dir / "batch_out" / device_name
        out_dir.mkdir(parents=True, exist_ok=True)
        wireless_json_path = out_dir / "wireless_device.json"
        if not args.dry_run:
            wireless_json_path.write_text(
                run(
                    [
                        "aws",
                        "--region",
                        args.region,
                        "iotwireless",
                        "get-wireless-device",
                        "--identifier",
                        wireless_id,
                        "--identifier-type",
                        "WirelessDeviceId",
                        "--output",
                        "json",
                    ],
                    output_path,
                    check=True,
                )
                + "\n",
                encoding="utf-8",
            )

        mfg_bin = out_dir / f"mfg_{device_name}.bin"
        mfg_hex = out_dir / f"mfg_{device_name}.hex"
        run(
            [
                "python3",
                str(provision_dir / "provision.py"),
                "nordic",
                "aws",
                "--wireless_device_json",
                str(wireless_json_path),
                "--device_profile_json",
                str(device_profile_json),
                "--output_bin",
                str(mfg_bin),
                "--output_hex",
                str(mfg_hex),
            ],
            output_path,
            check=not args.dry_run,
            dry_run=args.dry_run,
        )

        if args.dry_run:
            log("DRY_RUN: skipping device flash prompt.", output_path)
            continue

        log("Connect next device and press Enter to flash.", output_path)
        input()

        probe_id = detect_probe_id(output_path)

        if not args.skip_app_flash:
            run(
                [
                    "west",
                    "flash",
                    "--runner",
                    "pyocd",
                    "--build-dir",
                    args.build_dir,
                    "--",
                    "--target",
                    "nrf52840",
                    "--dev-id",
                    probe_id,
                ],
                output_path,
                check=True,
            )

        run(
            [
                "pyocd",
                "flash",
                "-t",
                "nrf52840",
                "-u",
                probe_id,
                str(mfg_hex),
            ],
            output_path,
            check=True,
        )

        time.sleep(1)

    log("Batch provisioning complete.", output_path)


if __name__ == "__main__":
    main()
