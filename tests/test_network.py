"""Run production Wi-Fi lifecycle logic with asynchronous event/clock adapters."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NetworkTests(unittest.TestCase):
    def test_lifecycle(self):
        output = ROOT / 'build/tests/network'
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'tests/network/support'), '-I' + str(ROOT / 'src'),
            *(str(ROOT / p) for p in ('tests/network/test_network.cpp', 'src/network/Network.cpp',
                                      'src/settings/Values.cpp', 'src/core/Text.cpp', 'src/core/Logic.cpp')),
            '-o', str(output)
        ], check=True)
        subprocess.run([str(output)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
