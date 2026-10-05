"""Exercise production release policy/HTTPS code with host transport adapters."""
import os
import pathlib
import subprocess
import tempfile
import unittest
from test_ota_signing import rsa, serialization

ROOT = pathlib.Path(__file__).resolve().parents[1]


def compile_and_run(name, sources, includes, args=(), libraries=(), flags=()):
    output = ROOT / 'build/tests' / name
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        *flags, *(f'-I{ROOT / path}' for path in includes), f'-I{ROOT / "src"}',
        *(str(ROOT / path) for path in sources), *libraries, '-o', str(output),
    ], check=True)
    subprocess.run([str(output), *map(str, args)], check=True,
                   env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


class ReleaseTests(unittest.TestCase):
    def test_release_policy_and_polling(self):
        compile_and_run('release-policy', [
            'tests/update/test_release.cpp', 'src/update/Release.cpp', 'src/update/AutoUpdate.cpp',
        ], [])

    def test_https_redirects(self):
        compile_and_run('release-http', [
            'tests/network/test_http.cpp', 'src/network/Http.cpp', 'src/update/Release.cpp',
        ], ['tests/network/support'])

    @unittest.skipIf(rsa is None, 'Release fixtures need Python cryptography')
    def test_firmware_worker(self):
        core = pathlib.Path(os.environ.get('NOVA_CORE', pathlib.Path.home() /
                            '.arduino15/packages/esp32/hardware/esp32/3.3.11'))
        firmware = ROOT / 'build/firmware/Nova.ino.bin'
        self.assertTrue(firmware.is_file(), 'Build firmware before release runtime tests')
        with tempfile.TemporaryDirectory(prefix='nova-release-test-') as directory:
            folder = pathlib.Path(directory)
            images = []
            for name in ('trusted', 'wrong'):
                private = rsa.generate_private_key(public_exponent=65537, key_size=2048)
                key = folder / (name + '.pem')
                key.write_bytes(private.private_bytes(serialization.Encoding.PEM,
                                serialization.PrivateFormat.TraditionalOpenSSL,
                                serialization.NoEncryption()))
                public = folder / (name + '-public.pem')
                public.write_bytes(private.public_key().public_bytes(serialization.Encoding.PEM,
                                   serialization.PublicFormat.SubjectPublicKeyInfo))
                image = folder / (name + '.bin')
                subprocess.run(['python3', str(core / 'tools/bin_signing.py'), '--bin', str(firmware),
                                '--key', str(key), '--out', str(image)], check=True, capture_output=True)
                images.append(image)
            compile_and_run('release-firmware', [
                'tests/update/test_firmware.cpp', 'src/update/Firmware.cpp', 'src/update/Release.cpp',
                'src/core/Text.cpp', 'src/core/Logic.cpp', 'src/settings/Values.cpp', 'src/settings/Store.cpp',
            ], ['tests/update/support', 'tests/storage/support'],
                [images[0], folder / 'trusted-public.pem', images[1]], ['-lcrypto', '-ljson-c'],
                ['-DNOVA_VERSION="0.0.0"'])


if __name__ == '__main__':
    unittest.main()
