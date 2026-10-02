#!/usr/bin/env python3

import argparse
import glob
import os
import select
import stat
import subprocess
import sys
import time


UEL = b"\x1b%-12345X"
FINAL_ESCAPE = b"\x1b"


def pjl_frame(command):
    return UEL + b"@PJL\r\n@PJL " + command.encode("ascii") + b"\r\n" + UEL


def pjl_value(response, name):
    prefix = name + "="
    value = ""
    for line in response.replace("\r", "\n").split("\n"):
        if line.startswith(prefix):
            value = line[len(prefix):].strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        value = value[1:-1]
    return value


def valid_identity(identity):
    return "MFG:Brother;" in identity and "MDL:HL-1110 series;" in identity


def run(command):
    return subprocess.run(command, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def device_identity(device):
    device_name = os.path.basename(device)
    identity_path = os.path.join("/sys/class/usbmisc", device_name,
                                 "device", "ieee1284_id")
    try:
        with open(identity_path, encoding="ascii") as identity_file:
            return identity_file.read().strip()
    except OSError as error:
        raise RuntimeError("Cannot read printer identity from {}: {}".format(
            identity_path, error))


def validate_device(device):
    try:
        mode = os.stat(device).st_mode
    except OSError as error:
        raise RuntimeError("Cannot access {}: {}".format(device, error))
    if not stat.S_ISCHR(mode):
        raise RuntimeError("{} is not a character device".format(device))

    identity = device_identity(device)
    if not valid_identity(identity):
        raise RuntimeError("{} is not a Brother HL-1110: {}".format(device, identity))
    return identity


def queue_state(queue):
    status_result = run(["lpstat", "-p", queue])
    if status_result.returncode != 0:
        raise RuntimeError(status_result.stderr.strip() or
                           "CUPS queue {} was not found".format(queue))
    jobs_result = run(["lpstat", "-o", queue])
    if jobs_result.returncode != 0:
        raise RuntimeError(jobs_result.stderr.strip() or
                           "Cannot inspect CUPS queue {}".format(queue))
    enabled = " disabled " not in status_result.stdout.lower()
    jobs = [line for line in jobs_result.stdout.splitlines() if line.strip()]
    return enabled, jobs, status_result.stdout.strip()


def write_command(device, command):
    descriptor = os.open(device, os.O_WRONLY | os.O_NONBLOCK)
    try:
        os.write(descriptor, pjl_frame(command))
        os.write(descriptor, FINAL_ESCAPE)
    finally:
        os.close(descriptor)


def read_available(descriptor, timeout):
    chunks = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        readable, _, _ = select.select([descriptor], [], [], remaining)
        if not readable:
            break
        try:
            chunk = os.read(descriptor, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            break
        chunks.append(chunk)
        deadline = time.monotonic() + 0.25
    return b"".join(chunks)


def query_status(device):
    descriptor = os.open(device, os.O_RDWR | os.O_NONBLOCK)
    response = []
    try:
        for command in ("INFO STATUS", "INFO PAGECOUNT", "INFO STATUS"):
            os.write(descriptor, pjl_frame(command))
            os.write(descriptor, FINAL_ESCAPE)
            response.append(read_available(descriptor, 2.0))
    finally:
        os.close(descriptor)
    return b"".join(response).decode("ascii", errors="replace")


def wait_for_printer(device, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(device):
            try:
                validate_device(device)
                return
            except RuntimeError:
                pass
        time.sleep(1)
    raise RuntimeError("Printer did not become available within {} seconds".format(timeout))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Safely reset toner, drum, and page counters on a Brother HL-1110.")
    parser.add_argument("--device", default="/dev/usb/lp0",
                        help="USB printer device (default: /dev/usb/lp0)")
    parser.add_argument("--queue", default="Brother_HL1110",
                        help="CUPS queue name (default: Brother_HL1110)")
    parser.add_argument("--check", action="store_true",
                        help="validate printer and queue without resetting")
    parser.add_argument("--yes", action="store_true",
                        help="confirm the irreversible reset non-interactively")
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        identity = validate_device(args.device)
        enabled, jobs, status = queue_state(args.queue)
        print(identity)
        print(status)

        if jobs:
            raise RuntimeError("CUPS queue has pending jobs:\n{}".format("\n".join(jobs)))
        if args.check:
            print("Printer and queue are ready for a reset.")
            return 0
        if os.geteuid() != 0:
            raise RuntimeError("Run this command as root (sudo).")

        print("WARNING: this irreversibly resets toner, drum, and page counters.")
        if not args.yes and input("Type RESET to continue: ").strip() != "RESET":
            print("Reset cancelled.")
            return 1

        if enabled:
            result = run(["cupsdisable", args.queue])
            if result.returncode != 0:
                raise RuntimeError(result.stderr.strip() or "Cannot disable CUPS queue")

        try:
            write_command(args.device, "EXECUTE REVIVALRESET")
            print("Reset command sent; waiting for the printer...")
            time.sleep(15)
            wait_for_printer(args.device)
            response = query_status(args.device)
            code = pjl_value(response, "CODE") or "unknown"
            display = pjl_value(response, "DISPLAY") or "unknown"
            page_count = pjl_value(response, "PAGECOUNT") or "unknown"
            print("Status: {} (PJL {}), page count: {}".format(display, code, page_count))
        finally:
            if enabled:
                result = run(["cupsenable", args.queue])
                if result.returncode != 0:
                    print("WARNING: could not re-enable CUPS queue: {}".format(
                        result.stderr.strip()), file=sys.stderr)
        return 0
    except (RuntimeError, OSError) as error:
        print("ERROR: {}".format(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())