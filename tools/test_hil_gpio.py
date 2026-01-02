#!/usr/bin/env python3
import argparse
import subprocess
import time
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--outfile", required=True)
    args = parser.parse_args()

    cmd = [
        "pyocd",
        "rtt",
        "-t",
        "nrf52840",
        "-u",
        args.probe,
    ]

    found_event = False
    found_send_ok = False
    found_run_id = False

    start = time.time()
    with open(args.outfile, "w", encoding="utf-8") as out:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            while time.time() - start < args.timeout:
                line = proc.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue
                out.write(line)
                out.flush()
                if "E2E run_id:" in line:
                    found_run_id = True
                if "GPIO event:" in line:
                    found_event = True
                if "Sidewalk send: ok" in line:
                    found_send_ok = True
                if found_event and found_send_ok:
                    break
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    if not found_event:
        print("FAIL: no GPIO event found in RTT output", file=sys.stderr)
        return 1
    if not found_send_ok:
        print("FAIL: no Sidewalk send ok in RTT output", file=sys.stderr)
        return 1
    if not found_run_id:
        print("WARN: no E2E run_id found in RTT output", file=sys.stderr)
    print("PASS: HIL GPIO test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
