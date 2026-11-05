#!/usr/bin/env python3
"""Back up, run SPEC-010 on the known board, and restore the original application."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import serial
from esptool.reset import HardReset

EXPECTED_MAC = 'd8:3b:da:4a:e9:84'
APP_OFFSET, APP_SIZE, FLASH_SIZE = 0x10000, 0x300000, 0x800000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Flash snapshots may contain credentials. Never create world-readable files.
    os.umask(0o077)
    args.output.mkdir(parents=True, exist_ok=False, mode=0o700)
    image = args.build / 'sprite_video_probe.bin'
    table = (args.build / 'partition_table/partition-table.bin').read_bytes()
    image_bytes = image.read_bytes()
    if not image_bytes or len(image_bytes) > APP_SIZE:
        raise ValueError('Invalid application image size')
    if len(table) > 4096:
        raise ValueError('Invalid partition table length')
    result = {'backup_verified': False, 'trial_complete': False, 'trial_ok': False,
              'original_app_verified': False, 'original_boot': False, 'original_mqtt': False,
              'serial_closed': True, 'physical_acceptance': 'pending', 'results': []}
    changed = False
    backup, original = args.output / 'before.bin', args.output / 'original-app.bin'

    def esptool(stage, *arguments):
        print('STAGE', stage, flush=True)
        command = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', args.port,
                   '--baud', '921600', '--after', 'no_reset', *map(str, arguments)]
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        (args.output / (stage + '.log')).write_bytes(proc.stdout)
        if proc.returncode:
            raise RuntimeError(stage + ' failed; inspect private log')
        return proc.stdout.decode(errors='replace')

    def monitor(trial):
        p = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        p.dtr = False
        p.rts = False
        p.port = args.port
        try:
            p.open()
            result['serial_closed'] = False
            p.dtr = False
            HardReset(p, uses_usb=True)()
            end = time.monotonic() + (60 if trial else 85)
            pending = b''
            with (args.output / ('trial.log' if trial else 'restored.log')).open('wb') as log:
                while time.monotonic() < end:
                    data = p.read(8192)
                    log.write(data)
                    log.flush()
                    pending += data
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        line = line.decode(errors='replace')
                        if trial and 'VIDEO_PROBE:' in line:
                            clean = re.sub(r'\x1b\[[0-9;]*m', '', line).strip()
                            print(clean, flush=True)
                            result['results'].append(clean)
                            if 'complete ok=' in line:
                                result['trial_complete'] = True
                                result['trial_ok'] = 'complete ok=1' in line
                        if any(s in line for s in ('Guru Meditation', 'Stack canary', 'assert failed',
                                                   'LCD color transmission timed out', 'abort() was called')):
                            result['runtime_error'] = True
                        if not trial:
                            if 'Desk Sprite ESP32-S3 Firmware' in line:
                                result['original_boot'] = True
                            if 'MQTT subscription acknowledged' in line:
                                result['original_mqtt'] = True
                    if trial and result['trial_complete']:
                        return
                    if not trial and result['original_boot'] and result['original_mqtt']:
                        print('PASS original application boot and MQTT subscription', flush=True)
                        return
        finally:
            p.close()
            result['serial_closed'] = not p.is_open

    try:
        identity = esptool('identity', 'read_mac')
        if EXPECTED_MAC not in identity.lower():
            raise ValueError('Board MAC mismatch; refusing to write')
        esptool('backup-read', 'read_flash', '0', hex(FLASH_SIZE), backup)
        data = backup.read_bytes()
        if len(data) != FLASH_SIZE:
            raise ValueError('Incomplete flash backup')
        esptool('backup-verify', 'verify_flash', '0', backup)
        result['backup_verified'] = True
        result['backup_sha256'] = hashlib.sha256(data).hexdigest()
        if data[0x8000:0x8000 + len(table)] != table:
            raise ValueError('Existing partition table differs; refusing trial')
        original.write_bytes(data[APP_OFFSET:APP_OFFSET + APP_SIZE])
        result['probe_sha256'] = hashlib.sha256(image_bytes).hexdigest()
        # Mark before starting: a failed write can already have changed sectors.
        changed = True
        esptool('trial-write', 'write_flash', hex(APP_OFFSET), image)
        esptool('trial-verify', 'verify_flash', hex(APP_OFFSET), image)
        print('PLAYBACK starts in 5 seconds after reset; three 8-second rounds', flush=True)
        monitor(True)
    except (Exception, KeyboardInterrupt) as exc:
        result['error'] = type(exc).__name__ + ': ' + str(exc)
        print('ERROR', type(exc).__name__, flush=True)
    finally:
        try:
            if changed:
                esptool('restore-write', 'write_flash', hex(APP_OFFSET), original)
                esptool('restore-verify', 'verify_flash', hex(APP_OFFSET), original)
                result['original_app_verified'] = True
            monitor(False)
        except (Exception, KeyboardInterrupt) as exc:
            result['restore_error'] = type(exc).__name__ + ': ' + str(exc)
            print('RESTORE ERROR', type(exc).__name__, flush=True)
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    passed = (result['trial_ok'] and result['original_app_verified'] and result['original_boot'] and
              result['original_mqtt'] and result['serial_closed'] and not result.get('runtime_error') and
              not result.get('error') and not result.get('restore_error'))
    print('TRIAL', 'PASS' if passed else 'INCOMPLETE', 'physical_acceptance=pending', flush=True)
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
