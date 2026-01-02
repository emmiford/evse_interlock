# Sidewalk Workspace

![Zephyr Unit Tests](https://github.com/REPLACE_OWNER/REPLACE_REPO/actions/workflows/zephyr-unit-tests.yml/badge.svg)

## Local Setup

This repository intentionally excludes secrets and provisioning artifacts. You must create them locally.

Missing-by-design files:
- `sidewalk/tools/provision/keys/certificate.json` (use `sidewalk/tools/provision/keys/certificate.json.example` as a template)
- `tools/devices_batch.csv` (use `tools/devices_batch.csv.example`)
- `tools/site_id_key.b64` (use `tools/site_id_key.b64.example`)
- Local AWS/Sidewalk outputs (`aws_*.json`, `aws_*_output.txt`, `bash_output*.txt`)

How to obtain/create required files:
- Sidewalk certificate JSON: download from AWS IoT Wireless/Sidewalk provisioning flow.
- Site ID key: run `python3 tools/site_id_from_address.py --generate-key`.
- Device batch CSV: copy `tools/devices_batch.csv.example` and fill your device details.

Build notes:
- Build outputs are generated under `build/` and are excluded from version control.

For security guidance, see `SECURITY.md`.

## Project Structure

- `app/sidewalk_end_device`: product-owned Sidewalk end-device app (formerly based on Sidewalk sample).
- `sidewalk/`: upstream Sidewalk SDK submodule (kept pristine; no local edits).
