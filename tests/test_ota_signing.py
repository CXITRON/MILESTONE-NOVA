"""Host check of the installed SDK signer against the built application.

This verifies the file/signature contract, not the device's flash or bootloader.
Ephemeral private keys and signed images stay in a temporary directory.
"""
import os
import json
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding, rsa
except ImportError:
    rsa = None


class OtaSigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if rsa is None:
            raise unittest.SkipTest("OTA signing check needs Python cryptography")
        core = pathlib.Path(os.environ.get(
            "NOVA_CORE", pathlib.Path.home() / ".arduino15/packages/esp32/hardware/esp32/3.3.11"
        ))
        cls.signer = core / "tools/bin_signing.py"
        cls.firmware = ROOT / "build/firmware/Nova.ino.bin"
        if not cls.signer.is_file() or not cls.firmware.is_file():
            raise unittest.SkipTest("Build firmware and set NOVA_CORE for the OTA signing check")

    def test_signed_firmware_and_rejection(self):
        firmware = self.firmware.read_bytes()
        self.assertEqual(firmware[0], 0xE9)  # ESP application header
        self.assertEqual(int.from_bytes(firmware[12:14], 'little'), 9)  # ESP32-S3
        self.assertEqual(firmware[80:112].split(b'\0')[0], b'MILESTONE-NOVA')
        self.assertNotEqual(firmware[48:80].split(b'\0')[0], b'')
        algorithm = padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=padding.PSS.MAX_LENGTH)
        for bits in (2048, 4096):
            with self.subTest(rsa_bits=bits), tempfile.TemporaryDirectory() as directory:
                base = pathlib.Path(directory)
                private = rsa.generate_private_key(public_exponent=65537, key_size=bits)
                private_path = base / "ephemeral-private.pem"
                private_path.write_bytes(private.private_bytes(
                    serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                    serialization.NoEncryption()
                ))
                public = private.public_key()
                public_pem = public.public_bytes(
                    serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
                )
                self.assertLess(len(public_pem), 1024)
                signed_path = base / "signed.bin"
                subprocess.run([
                    sys.executable, str(self.signer), "--bin", str(self.firmware),
                    "--key", str(private_path), "--out", str(signed_path)
                ], check=True, capture_output=True, text=True)
                release = base / "release"
                url = "https://raw.githubusercontent.com/CXITRON/MILESTONE-NOVA/main/releases/nova.bin"
                subprocess.run([
                    sys.executable, str(ROOT / "scripts/build/release.py"),
                    "--input", str(self.firmware), "--key", str(private_path),
                    "--core", str(self.signer.parents[1]), "--url", url,
                    "--output-dir", str(release)
                ], check=True, capture_output=True, text=True)
                manifest = json.loads((release / "stable.json").read_text())
                release_image = (release / "nova.bin").read_bytes()
                self.assertEqual(manifest["target"], "milestone-nova-s3")
                self.assertEqual(manifest["size"], len(release_image))
                self.assertEqual(manifest["sha256"], hashlib.sha256(release_image).hexdigest())
                public.verify(release_image[-512:][:bits // 8], firmware, algorithm, hashes.SHA256())
                signed = signed_path.read_bytes()
                self.assertEqual(len(signed), len(firmware) + 512)
                self.assertEqual(signed[:-512], firmware)
                # Use the modulus length, as UpdaterRSAVerifier does; a genuine
                # signature can itself end in zero, so rstrip would be incorrect.
                signature = signed[-512:][:bits // 8]
                self.assertEqual(signed[len(firmware) + bits // 8:], bytes(512 - bits // 8))
                public.verify(signature, firmware, algorithm, hashes.SHA256())
                changed = bytearray(firmware)
                changed[len(changed) // 2] ^= 1
                with self.assertRaises(InvalidSignature):
                    public.verify(signature, bytes(changed), algorithm, hashes.SHA256())
                wrong_signature = bytearray(signature)
                wrong_signature[0] ^= 1
                with self.assertRaises(InvalidSignature):
                    public.verify(bytes(wrong_signature), firmware, algorithm, hashes.SHA256())
                wrong_key = rsa.generate_private_key(public_exponent=65537, key_size=bits).public_key()
                with self.assertRaises(InvalidSignature):
                    wrong_key.verify(signature, firmware, algorithm, hashes.SHA256())


if __name__ == "__main__":
    unittest.main()
