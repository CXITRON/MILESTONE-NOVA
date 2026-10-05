"""Run the real boot-confirmation logic against host ESP-IDF adapters.

This does not emulate ESP flash or prove that a physical update boots; signature verification is
covered by test_ota_signing.py and the release flow by test_release.py.
"""
import os
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BootConfirmTests(unittest.TestCase):
    def test_boot_confirmation(self):
        output = ROOT / "build/tests/boot-confirm"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I" + str(ROOT / "tests/update/support"), "-I" + str(ROOT / "src"),
            str(ROOT / "tests/update/test_boot_confirm.cpp"),
            str(ROOT / "src/update/BootConfirm.cpp"), "-o", str(output)
        ], check=True)
        subprocess.run([str(output)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    unittest.main()
