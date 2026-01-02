# Security Notes

Do not commit any secrets, credentials, or provisioning artifacts to this repository.

Never commit:
- Sidewalk provisioning files (e.g., `sidewalk/tools/provision/keys/certificate.json`).
- Sidewalk manufacturing serial numbers (SMSN) and device private keys.
- AWS credentials, profiles, or tokens.
- Private keys or certificates (`*.pem`, `*.key`, `*.crt`, `*.p12`, etc.).
- Local device inputs (e.g., `tools/devices_batch.csv`, `tools/site_id_key.b64`).
- Local outputs/logs (`aws_*.json`, `aws_*_output.txt`, `bash_output*.txt`).

How to obtain required secrets:
- Sidewalk certificate JSON comes from the AWS IoT Wireless/Sidewalk provisioning process.
- Site ID encryption key is generated locally with `tools/site_id_from_address.py`.
- AWS credentials should be configured locally via your standard AWS CLI setup.

If you are unsure whether a file is sensitive, treat it as sensitive and exclude it.
