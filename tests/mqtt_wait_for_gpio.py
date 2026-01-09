#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import time


def get_endpoint(region):
    cmd = [
        "aws",
        "iot",
        "describe-endpoint",
        "--endpoint-type",
        "iot:Data-ATS",
        "--region",
        region,
    ]
    out = subprocess.check_output(cmd, text=True)
    data = json.loads(out)
    return data["endpointAddress"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pin", required=True)
    parser.add_argument("--topic", default="sidewalk/#")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--region", default=os.environ.get("AWS_REGION", "us-east-1"))
    parser.add_argument("--endpoint", default=os.environ.get("AWS_IOT_ENDPOINT"))
    parser.add_argument("--outfile", default=None)
    args = parser.parse_args()

    try:
        from awscrt import io, mqtt
        from awsiot import mqtt_connection_builder
    except ImportError:
        print("ERROR: awsiotsdk not installed. Run:", file=sys.stderr)
        print("  python3 -m pip install awsiotsdk", file=sys.stderr)
        return 2

    endpoint = args.endpoint or get_endpoint(args.region)
    event = io.EventLoopGroup(1)
    host_resolver = io.DefaultHostResolver(event)
    client_bootstrap = io.ClientBootstrap(event, host_resolver)

    connection = mqtt_connection_builder.websockets_with_default_aws_signing(
        endpoint=endpoint,
        client_bootstrap=client_bootstrap,
        region=args.region,
        clean_session=True,
        keep_alive_secs=30,
        client_id=f"sidewalk-e2e-{int(time.time())}",
    )

    def on_message_received(topic, payload, dup, qos, retain, **kwargs):
        text = payload.decode("utf-8", errors="replace")
        if f"\\\"pin\\\":\\\"{args.pin}\\\"" in text:
            received["ok"] = True
            received["payload"] = text

    received = {"ok": False, "payload": None}
    connection.connect().result()
    connection.subscribe(args.topic, mqtt.QoS.AT_LEAST_ONCE, on_message_received).result()

    start = time.time()
    while time.time() - start < args.timeout:
        if received["ok"]:
            break
        time.sleep(0.2)

    connection.disconnect().result()

    if not received["ok"]:
        print("FAIL: GPIO event not received within timeout", file=sys.stderr)
        return 1
    if args.outfile:
        with open(args.outfile, "w", encoding="utf-8") as out:
            out.write(received["payload"] or "")
    print("PASS: GPIO event received")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
