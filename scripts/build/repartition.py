#!/usr/bin/env python3
"""Prepare and perform the known NOVA USB NVS migration. Never erase the existing NVS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/repartition'
BUNDLE = WORK / 'prepared'
FLASH_SIZE = 0x1000000
TABLE_OFFSET = 0x8000


def layout(nvs_size, ota, app0, app1):
    return {'nvs': (1, 2, 0x9000, nvs_size, 0), 'otadata': (1, 0, ota, 0x2000, 0),
            'app0': (0, 0x10, app0, 0x600000, 0), 'app1': (0, 0x11, app1, 0x600000, 0)}


OLD20 = layout(0x5000, 0xE000, 0x10000, 0x610000)
OLD64 = layout(0x10000, 0x19000, 0x20000, 0x620000)
TARGET = layout(0x20000, 0x29000, 0x30000, 0x630000)
FILES = ('Nova.ino.bin', 'Nova.ino.partitions.bin', 'boot_app0.bin')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def partitions(data):
    found = {}
    checked = False
    for offset in range(0, len(data) - 31, 32):
        row = data[offset:offset + 32]
        if row[:2] == b'\xeb\xeb':
            if row[16:] != hashlib.md5(data[:offset]).digest():
                raise ValueError('Partition table checksum mismatch')
            checked = True
            break
        if row[:2] != b'\xaa\x50':
            raise ValueError('Invalid partition table entry')
        _, kind, subtype, start, size, raw_name, flags = struct.unpack('<HBBII16sI', row)
        name = raw_name.split(b'\0')[0].decode('ascii')
        if name in found or start + size > FLASH_SIZE:
            raise ValueError('Duplicate/out-of-range partition')
        found[name] = (kind, subtype, start, size, flags)
    if not checked:
        raise ValueError('Missing partition checksum')
    return found


def image_version(data):
    if (len(data) < 1024 or len(data) > TARGET['app0'][3] or data[0] != 0xE9
            or int.from_bytes(data[12:14], 'little') != 9
            or int.from_bytes(data[32:36], 'little') != 0xABCD5432
            or data[80:112].split(b'\0')[0] != b'MILESTONE-NOVA'):
        raise ValueError('Expected NOVA ESP32-S3 application image')
    version = data[48:80].split(b'\0')[0].decode('ascii')
    if not re.fullmatch(r'(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})', version):
        raise ValueError('Invalid app version')
    return version


def check_bundle(folder):
    manifest = json.loads((folder / 'manifest.json').read_text())
    if manifest['format'] != 1 or set(manifest['sha256']) != set(FILES):
        raise ValueError('Unknown bundle format')
    for name in FILES:
        if sha((folder / name).read_bytes()) != manifest['sha256'][name]:
            raise ValueError(f'Prepared file changed: {name}')
    if partitions((folder / FILES[1]).read_bytes()) != TARGET:
        raise ValueError('Prepared partition table is not the 128 KiB target')
    if image_version((folder / FILES[0]).read_bytes()) != manifest['version']:
        raise ValueError('Prepared app version mismatch')
    if len((folder / FILES[2]).read_bytes()) != 0x2000:
        raise ValueError('Incorrect OTA initialization image size')
    return manifest


def prepare(version):
    WORK.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='prepare-', dir=WORK) as temp:
        folder = Path(temp)
        for name in FILES:
            shutil.copyfile(ROOT / 'build/firmware' / name, folder / name)
        manifest = {'format': 1, 'version': version,
                    'sha256': {name: sha((folder / name).read_bytes()) for name in FILES}}
        (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        check_bundle(folder)
        BUNDLE.mkdir(exist_ok=True)
        for name in (*FILES, 'manifest.json'):
            os.replace(folder / name, BUNDLE / name)
    print(f'Prepared NOVA {version}, 128 KiB NVS: {BUNDLE}')
    print('USB command: scripts/build/repartition.sh /dev/ttyACM0')


def plan(old):
    if old == TARGET:
        raise ValueError('Already 128 KiB: migration refused; use normal firmware update')
    if old not in (OLD20, OLD64):
        raise ValueError('Unknown device partition layout; nothing will be written')
    old_end = old['nvs'][2] + old['nvs'][3]
    return old_end, TARGET['app0'][2] - old_end


def migrate(folder, backup, run, confirm=input):
    manifest = check_bundle(folder)
    table = backup / 'partition-before.bin'
    run('read-flash', hex(TABLE_OFFSET), '0x1000', str(table))
    old = partitions(table.read_bytes())
    erase_start, erase_size = plan(old)
    # Full flash backup preserves both old apps, bootloader, NVS/bonds, OTA state and spare space.
    full = backup / 'flash-before.bin'
    # Some USB-Serial/JTAG links stall on one continuous 16 MiB stub read. Bound reads,
    # then verify the assembled whole against flash before allowing any writes.
    chunk_size = 0x100000
    chunk = backup / 'read-chunk.bin'
    with full.open('xb') as output:
        for offset in range(0, FLASH_SIZE, chunk_size):
            run('read-flash', hex(offset), hex(chunk_size), str(chunk))
            block = chunk.read_bytes()
            if len(block) != chunk_size:
                raise ValueError('Incomplete flash backup chunk')
            output.write(block)
            chunk.unlink()
            if (offset + chunk_size) % 0x100000 == 0:
                print(f'Backup {(offset + chunk_size) // 0x100000}/16 MiB', flush=True)
        output.flush()
        os.fsync(output.fileno())
    data = full.read_bytes()
    if len(data) != FLASH_SIZE or data[TABLE_OFFSET:TABLE_OFFSET + 0x1000] != table.read_bytes():
        raise ValueError('Incomplete or inconsistent flash backup')
    run('verify-flash', '0', str(full))
    start, size = old['nvs'][2:4]
    saved_nvs = data[start:start + size]
    (backup / 'nvs-before.bin').write_bytes(saved_nvs)
    (backup / 'backup.json').write_text(json.dumps({
        'sha256': sha(data), 'flash_size': len(data), 'nvs_size': size,
        'target_version': manifest['version'], 'original_layout': old}, indent=2) + '\n')
    print(f'Verified full flash + NVS backup: {backup}')
    print(f'NVS {size // 1024} -> 128 KiB; firmware {manifest["version"]}.')
    print('Keep USB power connected. Recovery backup contains credentials; do not share it.')
    if confirm('Type MIGRATE to write, or Enter to cancel: ').strip() != 'MIGRATE':
        print('Cancelled without flash writes. Reconnect USB to resume the old firmware.')
        return False
    # Snapshot payloads were verified before contact; check again before any destructive command.
    check_bundle(folder)
    # Stay in the ROM loader between commands. Table is switched only after the new app/OTA verify.
    run('write-flash', hex(TARGET['app0'][2]), str(folder / FILES[0]))
    run('verify-flash', hex(TARGET['app0'][2]), str(folder / FILES[0]))
    run('erase-region', hex(erase_start), hex(erase_size))
    run('write-flash', hex(TARGET['otadata'][2]), str(folder / FILES[2]))
    run('verify-flash', hex(TARGET['otadata'][2]), str(folder / FILES[2]))
    # Ensure old credentials are untouched and newly added NVS pages are erased BEFORE table switch.
    after = backup / 'nvs-after.bin'
    run('read-flash', hex(start), hex(TARGET['nvs'][3]), str(after))
    if after.read_bytes() != saved_nvs + b'\xff' * (TARGET['nvs'][3] - size):
        raise ValueError('NVS preservation/expansion check failed; keep device in loader for recovery')
    run('write-flash', hex(TABLE_OFFSET), str(folder / FILES[1]))
    run('verify-flash', hex(TABLE_OFFSET), str(folder / FILES[1]))
    (backup / 'completed.json').write_text(json.dumps({'version': manifest['version'],
                                                     'nvs_size': TARGET['nvs'][3]}) + '\n')
    print('Flash migration verified. Reconnect USB to boot; retain the backup until device checks pass.')
    return True


def apply(port):
    check_bundle(BUNDLE)
    tool = os.environ.get('ESPTOOL')
    if not tool:
        candidates = list((Path.home() / '.arduino15/packages/esp32/tools/esptool_py').glob('*/esptool'))
        candidates.sort(key=lambda p: tuple(int(x) for x in p.parent.name.split('.')))
        if not candidates:
            raise ValueError('Set ESPTOOL to the esptool v5 executable')
        tool = str(candidates[-1])
    os.umask(0o077)
    WORK.mkdir(parents=True, exist_ok=True)
    # Snapshot avoids a later prepare replacing payloads halfway through flashing.
    backup = Path(tempfile.mkdtemp(prefix='usb-', dir=WORK))
    payload = backup / 'payload'
    shutil.copytree(BUNDLE, payload)
    print(f'USB session (private backup directory): {backup}', flush=True)
    def run(*args):
        # Per-block progress can fill a remote PTY and stall USB read acknowledgements.
        reading = args[0] == 'read-flash'
        options = ['--no-progress'] if reading else []
        # ROM reads avoid the observed USB-JTAG stub streaming failure. Writes retain
        # esptool's verified stub path; no command resets into the app between steps.
        loader = ['--no-stub'] if reading else []
        subprocess.run([tool, '--chip', 'esp32s3', '--port', port, '--after', 'no-reset',
                        *loader, *args, *options], check=True)
    try:
        migrate(payload, backup, run)
    except BaseException:
        print(f'Stopped. No automatic retry/reset. Retain backup: {backup}', flush=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('prepare').add_argument('--version', required=True)
    sub.add_parser('inspect')
    sub.add_parser('apply').add_argument('--port', required=True)
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.version)
    elif args.command == 'inspect':
        print(json.dumps(check_bundle(BUNDLE), indent=2))
    else:
        apply(args.port)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Repartition stopped: {error}')
