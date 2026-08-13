#!/usr/bin/env python3
"""
@file test_linux_pty.py
@brief Automated test for verifying SimRV Linux boot reaches interactive PTY/shell state.
"""
import os
import sys
import subprocess
import time

def main():
    simrv_bin = os.environ.get("SIMRV_BIN")
    if not simrv_bin:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.dirname(script_dir)
        simrv_bin = os.path.join(repo_root, "build", "rv64-release", "SimRV")

    images_dir = os.environ.get("SIMRV_IMAGES_DIR")
    if not images_dir:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.dirname(script_dir)
        images_dir = os.path.join(repo_root, "linux-images", "rv64")

    mem_img = os.environ.get("SIMRV_LINUX_MEM_IMG", os.path.join(images_dir, "fw_payload.bin"))
    disk_img = os.environ.get("SIMRV_LINUX_DISK_IMG", os.path.join(images_dir, "root.img"))
    dtb_img = os.environ.get("SIMRV_LINUX_DTB", os.path.join(images_dir, "devicetree.dtb"))
    timeout_secs = int(os.environ.get("SIMRV_TEST_TIMEOUT", "60"))

    if not os.path.exists(simrv_bin):
        print(f"Error: SimRV binary not found at '{simrv_bin}'", file=sys.stderr)
        sys.exit(2)

    if not os.path.exists(mem_img):
        print(f"Skip: Linux memory image not found at '{mem_img}'", file=sys.stderr)
        sys.exit(2)

    cmd = [
        simrv_bin,
        "-m", mem_img,
        "-D", disk_img,
        "-c", dtb_img,
        "--cli",
        "-e", "500000000"  # 500M instruction cap for fast automated testing
    ]

    print(f"Running Linux PTY boot test: {' '.join(cmd)}")
    start_time = time.time()

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    boot_markers = [
        "OpenSBI",
        "Booting Linux on hartid",
        "Linux version"
    ]

    found_markers = set()

    try:
        while True:
            if proc.poll() is not None:
                remaining_output, _ = proc.communicate(timeout=2)
                for line in remaining_output.splitlines():
                    print(line)
                    for marker in boot_markers:
                        if marker in line:
                            found_markers.add(marker)
                break

            line = proc.stdout.readline()
            if line:
                print(line, end="")
                for marker in boot_markers:
                    if marker in line:
                        found_markers.add(marker)

            if time.time() - start_time > timeout_secs:
                proc.kill()
                print(f"\nTimeout ({timeout_secs}s) exceeded waiting for Linux PTY boot markers.", file=sys.stderr)
                sys.exit(1)

    except Exception as e:
        proc.kill()
        print(f"\nExecution error: {e}", file=sys.stderr)
        sys.exit(1)

    elapsed = time.time() - start_time
    print(f"\nBoot completed in {elapsed:.2f}s. Detected markers: {found_markers}")

    if len(found_markers) >= 2:
        print("[PASS] Linux boot reachability verified.")
        sys.exit(0)
    else:
        print(f"[FAIL] Missing required boot markers. Only found {found_markers}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
