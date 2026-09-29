#!/usr/bin/env python3
"""Publish supplied player snapshots over NOVA BLE. Does not invent player data.

Each stdin line is JSON with title, artist, album, duration_ms, position_ms, playing.
All fields are required. Integrate an authorized OS player API upstream of this tool.
Optional dependency for transmission: bleak. --pack writes hex packets without BLE.
"""
import argparse
import asyncio
import json
import struct
import sys

INPUT_UUID = "33b2a140-9d4a-4e1b-b952-a1d398c30002"
CONTROL_UUID = "33b2a140-9d4a-4e1b-b952-a1d398c30003"


def packets(snapshot, transaction):
    for field in ("title", "artist", "album", "duration_ms", "position_ms", "playing"):
        if field not in snapshot:
            raise ValueError(f"Missing authoritative field: {field}")
    if type(snapshot["playing"]) is not bool:
        raise ValueError("playing must be boolean")
    for field in ("duration_ms", "position_ms"):
        if type(snapshot[field]) is not int or not 0 <= snapshot[field] <= 0xFFFFFFFF:
            raise ValueError(f"Invalid {field}")
    def header(kind):
        return struct.pack("<BBI", 1, kind, transaction)
    yield header(1)
    for kind, field in ((2, "title"), (3, "artist"), (4, "album")):
        if not isinstance(snapshot[field], str):
            raise ValueError(f"{field} must be text")
        text = snapshot[field].encode("utf-8")[:240].decode("utf-8", "ignore")
        chunks, current = [], bytearray()
        for char in text:
            raw = char.encode("utf-8")
            if len(current) + len(raw) > 14:
                chunks.append(bytes(current))
                current.clear()
            current.extend(raw)
        chunks.append(bytes(current))
        for chunk in chunks:
            yield header(kind) + chunk
    yield header(5) + struct.pack("<IIB", snapshot["position_ms"], snapshot["duration_ms"], snapshot["playing"])
    yield header(6)


async def send(address):
    from bleak import BleakClient
    async with BleakClient(address, pair=True) as client:
        def control(_, data):
            # The integrating application must execute this intent against its player.
            print(json.dumps({"control": data[0] if data else None}), file=sys.stderr, flush=True)
        await client.start_notify(CONTROL_UUID, control)
        transaction = 0
        while line := await asyncio.to_thread(sys.stdin.readline):
            # Materialize and validate everything before sending BEGIN.
            messages = list(packets(json.loads(line), transaction))
            for message in messages:
                await client.write_gatt_char(INPUT_UUID, message, response=True)
            transaction = (transaction + 1) & 0xFFFFFFFF


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address")
    parser.add_argument("--pack", action="store_true", help="Read JSON and print hexadecimal frames")
    args = parser.parse_args()
    if args.pack:
        for index, line in enumerate(sys.stdin):
            for packet in list(packets(json.loads(line), index & 0xFFFFFFFF)):
                print(packet.hex())
    elif args.address:
        asyncio.run(send(args.address))
    else:
        parser.error("provide --address or --pack")
