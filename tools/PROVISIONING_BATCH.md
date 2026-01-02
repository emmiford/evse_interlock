# Batch Sidewalk Provisioning

This workflow provisions multiple Sidewalk devices from a CSV and flashes them
one at a time. It does not create IoT Things.

## Inputs

CSV path (default): `tools/devices_batch.csv`

Required columns:
- `address` (canonical-ish format, e.g. `4223 Knox Ct, Denver, CO 80211`)
- `sidewalk_sn` (full SidewalkManufacturingSn, 64 hex chars) **OR**
- `certificate_json_path` (path to ACS certificate JSON containing `metadata.smsn`)

Example:
```
address,sidewalk_sn,certificate_json_path
4223 Knox Ct, Denver, CO 80211,2506270BC068CB9767F698F9548583B17C1DD6238EC26502A65167B789D54998,
4599 W 36th Pl, Denver, CO 80212,,/path/to/certificate_4599.json
```

Key file (auto-created): `tools/site_id_key.b64`

## Run

```
python3 tools/provision_batch.py > bash_output.txt 2>&1
```

The script:
- Generates `siteId` from address
- Creates Sidewalk wireless devices in AWS
- Exports device JSON and builds per-device MFG hex
- Pauses between devices so you can swap hardware
- Auto-detects probe ID when a single probe is connected

## Defaults (override via flags)

- Region: `us-east-1`
- Device profile: `d01c298b-0a71-4ec6-9cb6-33d8ae2286b9`
- Destination: `SensorAppDestination`
- Hardware rev: `RAK4630`
- Firmware rev: `v1.1.0-add-on`
- Device type: `EVSE`
- Customer ID: `default`

Run `python3 tools/provision_batch.py -h` for all options.

## Dry run

```
python3 tools/provision_batch.py --dry-run > bash_output.txt 2>&1
```
