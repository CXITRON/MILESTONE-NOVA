"""Run the same allocation-free effect engine used by the device."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LightingTests(unittest.TestCase):
    def test_effects(self):
        output = ROOT / 'build/tests/lighting'
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'src'), str(ROOT / 'tests/lighting/test_effects.cpp'),
            str(ROOT / 'src/lighting/Effects.cpp'), '-o', str(output)
        ], check=True)
        subprocess.run([str(output)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
