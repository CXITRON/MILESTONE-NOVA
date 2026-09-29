"""Assemble the actual C++ renderer's PPM outputs into a lossless PNG for QA."""
import pathlib
import struct
import sys
import zlib

folder = pathlib.Path(sys.argv[1])
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

files = sorted(folder.glob("screen-*.ppm"), key=lambda p: int(p.stem.split("-")[1]))
for group in range((len(files) + 8) // 9):
    page = files[group*9:(group+1)*9]
    width, height = 240*3, 320*((len(page)+2)//3)
    canvas = bytearray(width*height*3)
    for index, path in enumerate(page):
        data = path.read_bytes().split(b"\n", 3)[3]
        for row in range(320):
            start = ((index//3*320+row)*width+index%3*240)*3
            canvas[start:start+720] = data[row*720:(row+1)*720]
    raster = b"".join(b"\0"+canvas[row*width*3:(row+1)*width*3] for row in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width,height,8,2,0,0,0))
    png += chunk(b"IDAT", zlib.compress(raster)) + chunk(b"IEND", b"")
    output = folder / ("screens.png" if group == 0 else f"screens-{group+1}.png")
    output.write_bytes(png)
    print(output)
