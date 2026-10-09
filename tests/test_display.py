"""Execute the real display transport with a deterministic SPI clock adapter."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DisplayTests(unittest.TestCase):
    def test_transport(self):
        self.run_transport([])

    def test_dma_transport(self):
        self.run_transport(["-DARDUINO=10607", "-DNOVA_LCD_SYNC_TEST"])

    def run_transport(self, defines):
        output = ROOT / 'build/tests/display'
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'tests/display/support'), '-I' + str(ROOT / 'src'),
            str(ROOT / 'tests/display/test_display.cpp'), str(ROOT / 'src/display/Display.cpp'), str(ROOT / 'src/display/LcdBus.cpp'),
            *defines, '-o', str(output)
        ], check=True)
        subprocess.run([str(output)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    def test_async_transport(self):
        output = ROOT / 'build/tests/display-async'
        subprocess.run([os.environ.get('CXX','g++'), '-std=c++17','-Wall','-Wextra','-Werror',
            '-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
            '-DARDUINO=10607','-DARDUINO_RUNNING_CORE=1','-DNOVA_LCD_THREAD_TEST',
            '-I'+str(ROOT/'tests/display/support'),'-I'+str(ROOT/'src'),
            str(ROOT/'tests/display/test_async.cpp'),str(ROOT/'src/display/Display.cpp'),
            str(ROOT/'src/display/LcdBus.cpp'),'-o',str(output)],check=True)
        subprocess.run([str(output)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},timeout=30)
