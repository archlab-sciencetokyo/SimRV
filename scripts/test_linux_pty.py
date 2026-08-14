#!/usr/bin/env python3
"""
@file test_linux_pty.py
@brief Automated test for verifying SimRV Linux boot reaches interactive PTY/shell state.
"""
import os
import sys
import pty
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
    disk_img_default = os.path.join(images_dir, "root.img") if os.path.exists(os.path.join(images_dir, "root.img")) else os.path.join(images_dir, "root.bin")
    disk_img = os.environ.get("SIMRV_LINUX_DISK_IMG", disk_img_default)
    dtb_img = os.environ.get("SIMRV_LINUX_DTB", os.path.join(images_dir, "devicetree.dtb"))
    timeout_secs = int(os.environ.get("SIMRV_TEST_TIMEOUT", "90"))

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
        "-e", "3000000000"  # 3B instruction cap to reach init welcome banner
    ]

    print(f"Running Linux PTY boot test: {' '.join(cmd)}")
    start_time = time.time()

    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True
    )
    os.close(slave_fd)

    boot_markers = [
        "Welcome to SimRV Linux Boot",
        "Run /init as init process",
        "Booting Linux on hartid",
        "Please press Enter to activate this console",
        "login:",
    ]

    found_markers = set()
    buffer = ""

    try:
        while True:
            if proc.poll() is not None:
                break

            try:
                data = os.read(master_fd, 1024)
                if not data:
                    break
                text = data.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                buffer += text
                for marker in boot_markers:
                    if marker in buffer:
                        found_markers.add(marker)
                if "Welcome to SimRV Linux Boot" in found_markers:
                    # Target welcome banner reached!
                    break
            except OSError:
                break

            if time.time() - start_time > timeout_secs:
                proc.kill()
                print(f"\nTimeout ({timeout_secs}s) exceeded waiting for Linux PTY boot markers.", file=sys.stderr)
                sys.exit(1)

    except Exception as e:
        proc.kill()
        print(f"\nExecution error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        os.close(master_fd)
        if proc.poll() is None:
            proc.terminate()

    elapsed = time.time() - start_time
    print(f"\nBoot completed in {elapsed:.2f}s. Detected markers: {found_markers}")

    if ("Welcome to SimRV Linux Boot" in found_markers
            or "Run /init as init process" in found_markers
            or "Please press Enter to activate this console" in found_markers
            or "login:" in found_markers):
        print("[PASS] Linux boot reachability verified.")
        sys.exit(0)
    else:
        print(f"[FAIL] Missing required boot markers. Only found {found_markers}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
