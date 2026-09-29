#!/usr/bin/env python3
"""Embed offline portal assets as reproducible gzip data in the staging sketch."""
import gzip
import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:])
types = {'.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8',
         '.js': 'text/javascript; charset=utf-8'}
lines = ['#include "Assets.h"', 'namespace nova {']
entries = []
for path in sorted(source.iterdir()):
    if path.suffix not in types:
        continue
    name = f'asset{len(entries)}'
    data = gzip.compress(path.read_bytes(), mtime=0)
    lines.append(f'static const unsigned char {name}[] = {{')
    lines.extend(','.join(str(x) for x in data[i:i+32]) + ',' for i in range(0, len(data), 32))
    lines.append('};')
    route = '/' if path.name == 'index.html' else '/' + path.name
    entries.append(f'{{"{route}","{types[path.suffix]}",{name},sizeof({name})}}')
if not entries:
    raise SystemExit('No portal assets found')
lines += ['const PortalAsset portalAssets[] = {' + ',\n'.join(entries) + '};',
          'const size_t portalAssetCount = sizeof(portalAssets)/sizeof(portalAssets[0]);', '}']
output.write_text('\n'.join(lines) + '\n')
