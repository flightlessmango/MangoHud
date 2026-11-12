#!/usr/bin/env python3
"""Optional GameSir G7 Pro 8K battery reader for MangoHud's exec option.

Only the 3537:10c6 receiver in XInput mode has been tested. This program sends
the volatile enhanced-report heartbeat; it does not change saved settings or
detach a kernel driver. Run outside the game sandbox, as the desktop user.
"""

import argparse
import logging
import os
from pathlib import Path
import signal
import threading
import time


VENDOR_ID = 0x3537
PRODUCT_ID = 0x10C6
HEARTBEAT = [0x0F, 0xF2] + [0] * 62
REPORT_TIMEOUT = 5.0
MAX_AGE = 10.0


def default_output():
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return cache / "MangoHud" / "gamesir-battery.txt"


def parse_report(report):
    """Return display text, or None for a short, unrelated or empty report."""
    if len(report) != 64 or report[0] != 0x12:
        return None
    # An idle receiver can send all-zero payloads. A real controller has
    # populated stick bytes even when its battery is completely empty.
    if not any(report[1:]) or report[36] > 100:
        return None
    return f"{report[36]}%" + (" charging" if report[35] & 1 else "")


def publish(output, value):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + f".{os.getpid()}.tmp")
    try:
        temporary.write_text(value + "\n", encoding="utf-8")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def status(output):
    try:
        with output.open(encoding="utf-8") as stream:
            # Read contents and timestamp from the same atomic file version.
            age = time.time() - os.fstat(stream.fileno()).st_mtime
            value = stream.read(64).strip()
        if 0 <= age <= MAX_AGE and value:
            return value
    except OSError:
        pass
    return "Disconnected"


def read_device(device, output, stop, once=False):
    device.set_nonblocking(True)
    heartbeat = float("-inf")
    last_report = time.monotonic()
    last_publish = float("-inf")
    previous = None
    while not stop.is_set():
        now = time.monotonic()
        if now - last_report >= REPORT_TIMEOUT:
            raise OSError("No valid GameSir battery report for five seconds")
        if now - heartbeat >= 0.5:
            if device.write(HEARTBEAT) != len(HEARTBEAT):
                raise OSError("Could not send GameSir reporting heartbeat")
            heartbeat = now
        report = device.read(64)
        value = parse_report(report)
        if value is not None:
            if once:
                print(value, flush=True)
                return True
            last_report = now
            # Refresh even unchanged values so --status can reject stale files.
            if value != previous or now - last_publish >= 1:
                publish(output, value)
                previous = value
                last_publish = now
        if not report:
            stop.wait(0.02)
    return False


def run(hid, output, stop, once=False):
    last_error = None
    while not stop.is_set():
        if not once:
            publish(output, "Disconnected")
        try:
            # HIDAPI can enumerate multiple usages for one hidraw node.
            nodes = sorted({d["path"] for d in hid.enumerate(VENDOR_ID, PRODUCT_ID)})
            if once and not nodes:
                logging.error("No 3537:10c6 receiver found; connect in XInput mode")
                return 1
            for node in nodes:
                device = hid.device()
                try:
                    device.open_path(node)
                    if read_device(device, output, stop, once):
                        return 0
                except (OSError, ValueError) as error:
                    if str(error) != last_error:
                        logging.warning("%s: %s", os.fsdecode(node), error)
                        last_error = str(error)
                finally:
                    device.close()
                if not once:
                    publish(output, "Disconnected")
        except (OSError, ValueError) as error:
            logging.warning("Cannot enumerate GameSir receivers: %s", error)
        if once:
            return 1
        stop.wait(3)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=default_output(),
                        help="battery text file (default: %(default)s)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true",
                      help="print one hardware reading and exit without writing a file")
    mode.add_argument("--status", action="store_true",
                      help="print the cached reading, or Disconnected if stale/missing")
    args = parser.parse_args()
    if args.status:
        print(status(args.output))
        return 0
    try:
        import hid
    except ImportError:
        parser.error("Install the hidapi Python bindings (python-hidapi on Arch Linux)")
    if not hasattr(hid, "device"):
        parser.error("The hid module must provide hid.device() (the hidapi bindings)")
    stop = threading.Event()
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, lambda *_: stop.set())
    try:
        return run(hid, args.output, stop, args.once)
    finally:
        if not args.once:
            publish(args.output, "Disconnected")


if __name__ == "__main__":
    raise SystemExit(main())
