#!/usr/bin/env python3
import argparse
import subprocess
import time
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--logfile", required=True)
    args = parser.parse_args()

    cmd = [
        "pyocd",
        "rtt",
        "-t",
        "nrf52840",
        "-u",
        args.probe,
    ]

    start = time.time()
    run_id = None

    with open(args.logfile, "w", encoding="utf-8") as out:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        try:
            while time.time() - start < args.timeout:
                line = proc.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue
                out.write(line)
                out.flush()
                if "E2E run_id:" in line:
                    run_id = line.strip().split("E2E run_id:", 1)[1].strip()
                    break
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    if not run_id:
        print("ERROR: run_id not found in RTT output", file=sys.stderr)
        return 1

    print(run_id)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
