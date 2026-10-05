"""Execute the real display transport with a deterministic SPI clock adapter."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DisplayTests(unittest.TestCase):
    def test_transport(self):
        output = ROOT / 'build/tests/display'
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'tests/display/support'), '-I' + str(ROOT / 'src'),
            str(ROOT / 'tests/display/test_display.cpp'), str(ROOT / 'src/display/Display.cpp'),
            '-o', str(output)
        ], check=True)
        subprocess.run([str(output)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
