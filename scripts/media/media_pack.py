#!/usr/bin/env python3
"""Pack a cover/photo or image sequence into NOVA CRC-checked RGB565 files."""
import argparse
import pathlib
import struct
import zlib

SIDE = 160


def ppm(path):
    """Read binary PPM, including comments; useful without third-party modules."""
    raw = path.read_bytes()
    cursor = 0
    def token():
        nonlocal cursor
        while cursor < len(raw):
            if raw[cursor:cursor+1] == b"#":
                end = raw.find(b"\n", cursor)
                if end < 0:
                    raise ValueError("Unterminated PPM comment")
                cursor = end + 1
            elif raw[cursor] in b" \r\n\t":
                cursor += 1
            else:
                break
        start = cursor
        while cursor < len(raw) and raw[cursor] not in b" \r\n\t":
            cursor += 1
        return raw[start:cursor]
    if token() != b"P6":
        raise ValueError("Only P6 PPM supported without Pillow")
    width, height, maximum = int(token()), int(token()), int(token())
    if maximum != 255 or not 1 <= width <= 8192 or not 1 <= height <= 8192:
        raise ValueError("Invalid PPM dimensions or depth")
    if raw[cursor:cursor+2] == b"\r\n":
        cursor += 2
    elif cursor < len(raw) and raw[cursor] in b" \r\n\t":
        cursor += 1
    else:
        raise ValueError("Missing PPM raster separator")
    pixels = raw[cursor:]
    if len(pixels) != width * height * 3:
        raise ValueError("Truncated or oversized PPM raster")
    # Centre-crop to square, then bounded nearest-neighbour resize.
    size = min(width, height)
    left, top = (width - size) // 2, (height - size) // 2
    return b"".join(pixels[(start := ((top+y*size//SIDE)*width+left+x*size//SIDE)*3):start+3]
                    for y in range(SIDE) for x in range(SIDE))


def pixels565(path):
    path = pathlib.Path(path)
    if path.suffix.lower() == ".ppm":
        raw = ppm(path)
    else:
        try:
            from PIL import Image, ImageOps
        except ImportError as error:
            raise RuntimeError("PNG/JPEG input requires Pillow; P6 PPM needs only Python") from error
        with Image.open(path) as image:
            raw = ImageOps.fit(image.convert("RGB"), (SIDE, SIDE)).tobytes()
    data = bytearray()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i:i+3]
        data.extend(struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3))
    return data


def pack(inputs, output, fps=None):
    inputs = list(inputs)
    if not inputs or len(inputs) > 36000 or (fps is not None and not 1 <= fps <= 10):
        raise ValueError("Use 1..36000 frames and 1..10 FPS")
    if fps is None and len(inputs) != 1:
        raise ValueError("Images take exactly one input")
    if fps and 16 + len(inputs) * (SIDE * SIDE * 2 + 4) > 536870912:
        raise ValueError("Video exceeds the firmware's 512 MiB limit")
    output = pathlib.Path(output)
    if output.exists():
        raise FileExistsError(f"Refusing to replace {output}")
    with output.open("xb") as stream:
        try:
            if fps:
                stream.write(struct.pack("<4sHHHHI", b"NVV1", SIDE, SIDE, fps, 0, len(inputs)))
            for path in inputs:
                data = pixels565(path)
                crc = zlib.crc32(data)
                if fps:
                    stream.write(struct.pack("<I", crc))
                else:
                    stream.write(struct.pack("<4sHHII", b"NVI1", SIDE, SIDE, len(data), crc))
                stream.write(data)
        except BaseException:
            stream.close()
            output.unlink(missing_ok=True)
            raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--fps", type=int, help="NVV1 video; omit for NVI1 image")
    args = parser.parse_args()
    pack(args.inputs, args.output, args.fps)
