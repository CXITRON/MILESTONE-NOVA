#!/usr/bin/env python3
"""NOVA's length-delimited UTF-8 FNV-1a key; identical to core/Text.cpp."""
import argparse
import struct


def track_key(artist, title, album, duration_ms):
    result = 14695981039346656037
    content = bytearray()
    for field in (artist, title, album):
        raw = field.encode("utf-8")
        content.extend(struct.pack("<I", len(raw)))
        content.extend(raw)
    content.extend(struct.pack("<I", duration_ms))
    for byte in content:
        result = ((result ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{result:016x}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artist", default="")
    parser.add_argument("--title", required=True)
    parser.add_argument("--album", default="")
    parser.add_argument("--duration-ms", type=int, default=0)
    args = parser.parse_args()
    print(track_key(args.artist, args.title, args.album, args.duration_ms))
