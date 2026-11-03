#!/usr/bin/env python3
"""Run SPEC-007's RAM-only probe, then attempt reset and close the serial port."""
import argparse
import hashlib
import json
import time
from pathlib import Path

from esptool.bin_image import LoadFirmwareImage
from esptool.targets.esp32s3 import ESP32S3ROM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fw = LoadFirmwareImage("esp32s3", str(args.image))
    # Reject images containing mapped Flash segments before touching the device.
    for seg in fw.segments:
        if not ((0x3FC88000 <= seg.addr < 0x3FD00000 and seg.addr+len(seg.data) <= 0x3FD00000)
                or (0x40370000 <= seg.addr < 0x403E0000 and seg.addr+len(seg.data) <= 0x403E0000)):
            raise ValueError("Image contains a non-RAM segment")
    result = {"image_sha256": hashlib.sha256(args.image.read_bytes()).hexdigest(),
              "transport": "ROM RAM loader; no Flash write commands", "lines": [],
              "probe_complete": False, "hard_reset_command_completed": False,
              "flash_application_start_verified": False, "serial_closed": None}
    esp = None
    try:
        esp = ESP32S3ROM(args.port, 115200)
        esp.connect()
        for seg in fw.segments:
            size = len(seg.data)
            block = esp.ESP_RAM_BLOCK
            esp.mem_begin(size, (size+block-1)//block, block, seg.addr)
            for seq, offset in enumerate(range(0, size, block)):
                esp.mem_block(seg.data[offset:offset+block], seq)
        esp.mem_finish(fw.entrypoint)
        esp._port.timeout = 0.2
        deadline = time.monotonic()+75
        pending = b""
        while time.monotonic() < deadline:
            pending += esp._port.read(4096)
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                decoded = line.decode("utf-8", errors="replace").strip()
                if decoded.startswith("PROBE "):
                    result["lines"].append(decoded)
                    print(decoded, flush=True)
            if "PROBE done returning_to_flash" in result["lines"]:
                result["probe_complete"] = True
                break
    except (Exception, KeyboardInterrupt) as exc:
        # Keep only error metadata; do not include arbitrary serial payloads.
        result["probe_error"] = {"type": type(exc).__name__, "errno": getattr(exc, "errno", None)}
    finally:
        if esp is not None:
            try:
                esp.hard_reset()
                result["hard_reset_command_completed"] = True
            except Exception as exc:
                result["reset_error"] = {"type": type(exc).__name__, "errno": getattr(exc, "errno", None)}
            try:
                esp._port.close()
                result["serial_closed"] = True
            except Exception as exc:
                result["serial_closed"] = False
                result["close_error"] = {"type": type(exc).__name__, "errno": getattr(exc, "errno", None)}
        args.output.write_text(json.dumps(result, indent=2)+"\n")
    if (not result["probe_complete"] or "probe_error" in result
            or not result["hard_reset_command_completed"] or result["serial_closed"] is not True):
        raise SystemExit("Probe or cleanup incomplete; inspect result and confirm the device display.")


if __name__ == "__main__":
    main()
