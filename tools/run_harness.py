#!/usr/bin/env python3
"""
One-click Build, Flash, and Test Harness Runner for Wio Terminal.
Usage:
    python tools/run_harness.py           # Run harness tests directly on connected device
    python tools/run_harness.py --flash   # Compile, flash, and then run harness tests
"""

import sys
import shutil
import subprocess
import argparse
import time
from pathlib import Path
from test_harness import find_wio_port, WioHarnessClient, run_tests

REPO_ROOT = Path(__file__).resolve().parent.parent


def run_cmd(cmd, desc):
    print(f"\n[EXEC] {desc}...")
    res = subprocess.run(cmd, shell=True, text=True, capture_output=True)
    if res.returncode != 0:
        print(f"[ERROR] {desc} failed (exit {res.returncode}):")
        if res.stdout:
            print(res.stdout)
        if res.stderr:
            print(res.stderr)
        sys.exit(1)
    return res.stdout


def main():
    parser = argparse.ArgumentParser(description="One-click Build, Flash, and Test Harness")
    parser.add_argument("--flash", action="store_true", help="Compile and upload firmware before testing")
    parser.add_argument("--port", type=str, default=None, help="Serial port (e.g. COM3)")
    args = parser.parse_args()

    port = args.port or find_wio_port()
    if not port and not args.flash:
        print("[ERROR] Wio Terminal not detected on serial port.")
        sys.exit(1)

    arduino_cli = shutil.which("arduino-cli") or "C:\\Program Files\\Arduino CLI\\arduino-cli.exe"
    libs_dir = REPO_ROOT / "libraries"
    pet_dir = REPO_ROOT / "pet"

    if args.flash:
        # Step 1: Compile
        run_cmd(
            f'"{arduino_cli}" compile --libraries "{libs_dir}" --fqbn Seeeduino:samd:seeed_wio_terminal "{pet_dir}"',
            "Compiling pet firmware"
        )

        # Step 2: Upload
        if not port:
            port = find_wio_port() or "COM3"
        run_cmd(
            f'"{arduino_cli}" upload -p {port} --fqbn Seeeduino:samd:seeed_wio_terminal "{pet_dir}"',
            f"Flashing firmware to {port}"
        )

        print("[WAIT] Waiting for Wio Terminal to reboot and initialize...")
        time.sleep(2.0)
        port = find_wio_port() or port
        # 等待固件 setup() 连网预热完毕 (最长 10 秒)
        t_boot = time.time()
        ready = False
        while time.time() - t_boot < 10.0:
            try:
                probe = WioHarnessClient(port, timeout=0.5)
                probe.connect()
                if probe.ping(timeout=0.4):
                    ready = True
                    probe.close()
                    break
                probe.close()
            except Exception:
                pass
            time.sleep(0.5)
        if ready:
            print("[INFO] Wio Terminal booted and ready!")
        else:
            print("[WARN] Warmup took longer than expected, continuing...")

    print("\n==================================================")
    print(f"  Starting Test Harness Suite on {port}")
    print("==================================================")

    client = WioHarnessClient(port)
    try:
        client.connect()
    except Exception as e:
        print(f"[ERROR] Could not connect to {port}: {e}")
        sys.exit(1)

    try:
        results = run_tests(client)
    finally:
        client.close()

    total = len(results)
    passed = sum(1 for r in results if r.passed)
    failed = total - passed

    print("\n--------------------------------------------------")
    print(f"Summary: Total: {total} | Passed: {passed} | Failed: {failed}")
    if failed == 0:
        print("\033[92m>>> ALL CHECKS PASSED: Firmware is solid & ready for release! <<<\033[0m")
        sys.exit(0)
    else:
        print(f"\033[91m>>> REGRESSION DETECTED: {failed} test(s) failed! <<<\033[0m")
        sys.exit(1)


if __name__ == "__main__":
    main()
