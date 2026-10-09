import os
from pathlib import Path
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[1]
class JpegTests(unittest.TestCase):
    def test_fast_decoder(self):
        out=ROOT/'build/tests/jpeg'
        out.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-g','-O1',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-DNOVA_FAST_JPEG','-D__LINUX__',
            '-I'+str(ROOT/'tests/storage/support'),'-I'+str(ROOT/'src'),
            '-I'+str(ROOT/'lib/jpegdec/src'),str(ROOT/'tests/media/test_jpeg.cpp'),
            str(ROOT/'src/media/JpegImage.cpp'),str(ROOT/'tests/storage/support/Jpeg.cpp'),
            str(ROOT/'lib/jpegdec/src/JPEGDEC.cpp'),'-ljpeg','-o',str(out)],check=True)
        subprocess.run([str(out)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
