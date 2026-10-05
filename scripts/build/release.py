#!/usr/bin/env python3
"""Prepare an independent NOVA signed image and manifest; never publish or flash.

Uses the installed ESP32 SDK signer. The private key stays at the caller's path.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

TARGET = "milestone-nova-s3"
PREFIX = "/CXITRON/MILESTONE-NOVA/releases/download/"
VERSION = re.compile(r"(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})")


def image_version(data):
    if (len(data) < 1024 or len(data) + 512 > 6291456 or data[0] != 0xE9
            or int.from_bytes(data[12:14], "little") != 9
            or int.from_bytes(data[32:36], "little") != 0xABCD5432
            or data[80:112].split(b"\0")[0] != b"MILESTONE-NOVA"):
        raise ValueError("Input must be the unsigned NOVA ESP32-S3 application image")
    version = data[48:80].split(b"\0")[0].decode("ascii")
    if not VERSION.fullmatch(version):
        raise ValueError("Stable application version must be major.minor.patch")
    return version


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=root / "build/firmware/Nova.ino.bin")
    parser.add_argument("--key", type=Path, required=True, help="RSA-2048..4096 private PEM")
    parser.add_argument("--url", required=True, help="GitHub Releases download URL under the matching v<version> tag")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=Path(os.environ.get(
        "NOVA_CORE", Path.home() / ".arduino15/packages/esp32/hardware/esp32/3.3.11")))
    args = parser.parse_args()
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding, rsa

        firmware = args.input.read_bytes()
        version = image_version(firmware)
        url = urlsplit(args.url)
        expected = PREFIX + "v" + version + "/"
        image_name = url.path[len(expected):] if url.path.startswith(expected) else ""
        if (url.scheme != "https" or url.netloc != "github.com"
                or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,91}\.bin", image_name)
                or url.query or url.fragment or len(args.url) >= 256):
            raise ValueError("URL must identify a NOVA GitHub Releases .bin under v" + version)
        key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
        if not isinstance(key, rsa.RSAPrivateKey) or not 2048 <= key.key_size <= 4096:
            raise ValueError("Expected an RSA-2048..4096 private key")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        paths = [args.output_dir / image_name, args.output_dir / "stable.json"]
        if any(path.exists() for path in paths):
            raise ValueError("Output already exists; choose a new output directory")
        with tempfile.TemporaryDirectory(prefix="nova-release-") as temp:
            signed_path = Path(temp) / "signed.bin"
            subprocess.run([sys.executable, str(args.core / "tools/bin_signing.py"),
                            "--bin", str(args.input), "--key", str(args.key),
                            "--out", str(signed_path)], check=True, capture_output=True)
            signed = signed_path.read_bytes()
            if len(signed) != len(firmware) + 512 or signed[:-512] != firmware:
                raise ValueError("SDK signer produced an incompatible image")
            key.public_key().verify(
                signed[-512:][:key.key_size // 8], firmware,
                padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=padding.PSS.MAX_LENGTH),
                hashes.SHA256())
            manifest = {"target": TARGET, "version": version, "url": args.url,
                        "size": len(signed), "sha256": hashlib.sha256(signed).hexdigest()}
            paths[0].write_bytes(signed)
            paths[1].write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Verified release artifacts: {paths[0]} and {paths[1]}")
    except (ValueError, OSError, ImportError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
