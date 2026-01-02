#!/usr/bin/env python3
import argparse
import base64
import hashlib
import os
import re
import sys
from typing import Optional



TOKEN_MAP = {
    "ST": "STREET",
    "ST.": "STREET",
    "AVE": "AVENUE",
    "AVE.": "AVENUE",
    "AV": "AVENUE",
    "AV.": "AVENUE",
    "RD": "ROAD",
    "RD.": "ROAD",
    "DR": "DRIVE",
    "DR.": "DRIVE",
    "CT": "COURT",
    "CT.": "COURT",
    "LN": "LANE",
    "LN.": "LANE",
    "BLVD": "BOULEVARD",
    "BLVD.": "BOULEVARD",
    "PL": "PLACE",
    "PL.": "PLACE",
    "TER": "TERRACE",
    "TER.": "TERRACE",
    "PKWY": "PARKWAY",
    "PKWY.": "PARKWAY",
    "HWY": "HIGHWAY",
    "HWY.": "HIGHWAY",
    "CIR": "CIRCLE",
    "CIR.": "CIRCLE",
    "CTR": "CENTER",
    "CTR.": "CENTER",
    "APT": "UNIT",
    "APT.": "UNIT",
    "STE": "UNIT",
    "STE.": "UNIT",
    "SUITE": "UNIT",
    "UNIT": "UNIT",
}


def canonicalize_address(address: str) -> str:
    cleaned = address.upper().strip()
    cleaned = re.sub(r"[^A-Z0-9]+", " ", cleaned)
    tokens = []
    for tok in cleaned.split():
        mapped = TOKEN_MAP.get(tok, tok)
        tokens.append(mapped)
    return " ".join(tokens)


INVALID_FORMAT = "INVALID_FORMAT"


def _parse_us_address(address: str) -> Optional[dict]:
    address = address.upper()
    parts = [p.strip() for p in address.split(",") if p.strip()]
    if len(parts) < 3:
        return None

    street = parts[0]
    unit = ""
    if len(parts) > 3:
        unit = ", ".join(parts[1:-2])
    city = parts[-2]
    state_zip = parts[-1].upper()
    match = re.search(r"([A-Z]{2})\s*(\d{5})(?:-?(\d{4}))?$", state_zip)
    if not match:
        return None
    state, zip5, zip4 = match.group(1), match.group(2), match.group(3) or ""
    return {
        "street": street,
        "unit": unit,
        "city": city,
        "state": state,
        "zip5": zip5,
        "zip4": zip4,
    }


def normalize_canonical_address(address: str) -> Optional[str]:
    parsed = _parse_us_address(address)
    if not parsed:
        return INVALID_FORMAT
    zip_full = f"{parsed['zip5']}-{parsed['zip4']}" if parsed["zip4"] else parsed["zip5"]
    unit = f", {parsed['unit']}" if parsed["unit"] else ""
    return (
        f"{parsed['street']}{unit}, {parsed['city']}, "
        f"{parsed['state']} {zip_full}"
    )


def _require_pycryptodome():
    try:
        from Crypto.Cipher import AES  # noqa: F401
    except Exception:
        print(
            "Missing pycryptodome. Install with: python3 -m pip install pycryptodome",
            file=sys.stderr,
        )
        sys.exit(2)


def _load_key(key_b64: str) -> bytes:
    key = base64.b64decode(key_b64)
    if len(key) not in (32, 48, 64):
        raise ValueError("Key must be 32/48/64 bytes for AES-SIV.")
    return key


def site_id_encrypt(canonical: str, customer_id: str, key_b64: str) -> str:
    _require_pycryptodome()
    from Crypto.Cipher import AES

    key = _load_key(key_b64)
    cipher = AES.new(key, AES.MODE_SIV)
    cipher.update(customer_id.encode("utf-8"))
    ciphertext, tag = cipher.encrypt_and_digest(canonical.encode("utf-8"))
    blob = tag + ciphertext
    site_id = base64.b32encode(blob).decode("ascii").rstrip("=").lower()
    return site_id


def site_id_decrypt(site_id: str, customer_id: str, key_b64: str) -> str:
    _require_pycryptodome()
    from Crypto.Cipher import AES

    key = _load_key(key_b64)
    padded = site_id.upper()
    padded += "=" * ((8 - len(padded) % 8) % 8)
    blob = base64.b32decode(padded.encode("ascii"))
    if len(blob) < 17:
        raise ValueError("siteId too short to decrypt; use the full siteId.")
    tag, ciphertext = blob[:16], blob[16:]
    cipher = AES.new(key, AES.MODE_SIV)
    cipher.update(customer_id.encode("utf-8"))
    plaintext = cipher.decrypt_and_verify(ciphertext, tag)
    return plaintext.decode("utf-8")


def generate_key_b64() -> str:
    _require_pycryptodome()
    return base64.b64encode(os.urandom(64)).decode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a stable siteId from a canonicalized address."
    )
    # Short serial convention (outside this script): use last 8 chars of the
    # Sidewalk Manufacturing SN when you need a compact device suffix.
    parser.add_argument(
        "address",
        nargs="?",
        help="Free-form address string (required for encrypt).",
    )
    parser.add_argument(
        "--customer-id",
        default="default",
        help="Customer ID to namespace the siteId.",
    )
    parser.add_argument(
        "--length",
        type=int,
        default=0,
        help="Optional short alias length; full siteId is always printed.",
    )
    parser.add_argument(
        "--key-b64",
        default=os.getenv("SITE_ID_KEY_B64", ""),
        help="Base64 AES-SIV key (or set SITE_ID_KEY_B64).",
    )
    parser.add_argument(
        "--decrypt",
        action="store_true",
        help="Decrypt a siteId back to canonical address.",
    )
    parser.add_argument(
        "--gen-key",
        action="store_true",
        help="Generate a new base64 AES-SIV key.",
    )
    args = parser.parse_args()

    if args.gen_key:
        print(generate_key_b64())
        return

    if args.decrypt:
        if not args.key_b64:
            raise SystemExit("Missing --key-b64 (or SITE_ID_KEY_B64).")
        if not args.address:
            raise SystemExit("Provide the siteId string as the address arg.")
        canonical = site_id_decrypt(args.address, args.customer_id, args.key_b64)
        print(f"canonical: {canonical}")
        return

    if not args.address:
        raise SystemExit("Missing address.")
    standardized = normalize_canonical_address(args.address)
    if standardized == INVALID_FORMAT:
        raise SystemExit(
            "Invalid format. Use: 'street, city, ST ZIP' "
            "(example: 4223 Knox Ct, Denver, CO 80211). "
            "For units: 'street, unit, city, ST ZIP'."
        )
    canonical = canonicalize_address(standardized)

    if not args.key_b64:
        raise SystemExit("Missing --key-b64 (or SITE_ID_KEY_B64).")
    site_id_full = site_id_encrypt(canonical, args.customer_id, args.key_b64)
    print(f"siteId_full: {site_id_full}")
    if args.length:
        print(f"siteId_short: {site_id_full[:args.length]}")
    print(f"canonical: {canonical}")
    print(f"standardized: {standardized}")


if __name__ == "__main__":
    main()
