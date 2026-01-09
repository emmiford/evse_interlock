#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import time


def run_aws(cmd):
    out = subprocess.check_output(cmd, text=True)
    return json.loads(out)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--run-id", default=None)
    parser.add_argument("--event-id", default=None)
    parser.add_argument("--table", default=None)
    parser.add_argument("--region", default=None)
    parser.add_argument("--since-ms", type=int, default=None)
    parser.add_argument("--limit", type=int, default=25)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--poll-interval", type=int, default=5)
    parser.add_argument("--ttl-skew-seconds", type=int, default=3600)
    return parser.parse_args()


def main():
    args = parse_args()
    region = args.region or "us-east-1"
    table = args.table or "sidewalk-v1-device_events_v2"
    since_ms = args.since_ms or int(time.time() * 1000) - 10 * 60 * 1000
    deadline = time.time() + args.timeout
    match = None
    now_ms = None

    while time.time() < deadline and not match:
        now_ms = int(time.time() * 1000)
        cmd = [
            "aws",
            "--region",
            region,
            "dynamodb",
            "query",
            "--table-name",
            table,
            "--key-condition-expression",
            "device_id = :d AND #ts BETWEEN :s AND :e",
            "--expression-attribute-names",
            '{"#ts":"timestamp"}',
            "--expression-attribute-values",
            json.dumps(
                {
                    ":d": {"S": args.device_id},
                    ":s": {"N": str(since_ms)},
                    ":e": {"N": str(now_ms)},
                }
            ),
            "--limit",
            str(args.limit),
        ]

        data = run_aws(cmd)
        items = data.get("Items", [])
        for item in items:
            run_id = item.get("run_id", {}).get("S")
            if args.run_id is not None and run_id != args.run_id:
                continue
            if args.event_id:
                if item.get("event_id", {}).get("S") != args.event_id:
                    continue
            match = item
            break

        if not match:
            time.sleep(args.poll_interval)

    if not match:
        print("FAIL: no matching event found in DynamoDB query window", file=sys.stderr)
        return 1

    def require_attr(attr_name):
        if attr_name not in match:
            print(f"FAIL: missing {attr_name}", file=sys.stderr)
            return False
        return True

    ok = True
    ok &= require_attr("schema_version")
    ok &= require_attr("device_id")
    ok &= require_attr("event_id")
    ok &= require_attr("timestamp")
    ok &= require_attr("device_type")
    ok &= require_attr("data")

    ttl_val = match.get("ttl", {}).get("N")
    if not ttl_val:
        print("FAIL: missing ttl", file=sys.stderr)
        ok = False
    else:
        ttl_int = int(ttl_val)
        if ttl_int > 10_000_000_000:
            print("FAIL: ttl appears to be ms, not seconds", file=sys.stderr)
            ok = False
        ts_val = match.get("timestamp", {}).get("N")
        if ts_val:
            expected_ttl = int(int(ts_val) / 1000) + 90 * 24 * 60 * 60
            if abs(ttl_int - expected_ttl) > args.ttl_skew_seconds:
                print("FAIL: ttl not aligned with timestamp", file=sys.stderr)
                ok = False

    schema_version = match.get("schema_version", {}).get("S")
    if schema_version and schema_version != "1.0":
        print(f"FAIL: unexpected schema_version {schema_version}", file=sys.stderr)
        ok = False

    if not ok:
        return 1

    print("PASS: DynamoDB verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
