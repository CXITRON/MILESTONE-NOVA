"""Publish firmware renderer frames as lossless PNG sheets and named galleries."""
import collections
import html
import pathlib
import struct
import sys
import zlib

folder = pathlib.Path(sys.argv[1])


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(data, width, height):
    raster = b"".join(b"\0" + data[row*width*3:(row+1)*width*3] for row in range(height))
    result = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    return result + chunk(b"IDAT", zlib.compress(raster)) + chunk(b"IEND", b"")


scenes = []
for line in (folder / "scenes.tsv").read_text().splitlines():
    name, group, title = line.split("\t")
    path = folder / name
    header, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    if (header, dimensions, maximum) != (b"P6", b"240 320", b"255") or len(data) != 240*320*3:
        raise ValueError(f"Invalid rendered frame: {path}")
    image = path.with_suffix(".png")
    image.write_bytes(png(data, 240, 320))
    scenes.append((image.name, group, title, data))
if not scenes:
    raise ValueError("No rendered scenes")

for group in range((len(scenes) + 8) // 9):
    page = scenes[group*9:(group+1)*9]
    width, height = 240*3, 320*((len(page)+2)//3)
    canvas = bytearray(width*height*3)
    for index, (_, _, _, data) in enumerate(page):
        for row in range(320):
            start = ((index//3*320+row)*width+index%3*240)*3
            canvas[start:start+720] = data[row*720:(row+1)*720]
    output = folder / ("screens.png" if group == 0 else f"screens-{group+1}.png")
    output.write_bytes(png(canvas, width, height))
    print(output)

groups = collections.defaultdict(list)
for number, (name, group, title, _) in enumerate(scenes, 1):
    groups[group].append((number, name, title))

description = (
    f"현재 펌웨어의 C++ UI와 실제 폰트로 렌더링한 {len(scenes)}개 장면입니다. "
    "각 화면은 240 × 320이며 날짜·곡·센서·진단 값과 아트는 예시 데이터입니다. "
    "정지 이미지이므로 스크롤·영상·전환 애니메이션과 실물 LCD 색감은 포함하지 않습니다."
)
document = ['<!doctype html><html lang="ko"><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,initial-scale=1">',
            '<title>MILESTONE NOVA · 화면 미리보기</title>',
            '''<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101418;color:#e8eeee}
*{box-sizing:border-box}body{margin:0 auto;max-width:1440px;padding:32px 24px 72px}
header{max-width:860px}h1{font-size:28px;margin:0 0 16px}p{line-height:1.7;color:#a4b5ba}
nav{display:flex;gap:10px;flex-wrap:wrap;padding:16px 0 28px}
a{color:#8de2cc}nav a{padding:8px 14px;border:1px solid #304440;border-radius:24px;text-decoration:none}
section{padding-top:20px}h2{font-size:21px}h2 span{color:#91a4a7;font-size:14px;margin-left:10px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:24px}
figure{margin:0;padding:16px;background:#182126;border:1px solid #29363b;border-radius:12px}
figure a{display:block;width:240px;margin:auto;max-width:100%}
img{display:block;width:240px;height:auto;max-width:100%;image-rendering:pixelated}
figcaption{font-size:14px;line-height:1.6;padding-top:14px;min-height:56px;color:#d9e3e4}
figcaption span{color:#85aa9e;margin-right:8px}a:focus-visible{outline:2px solid #8de2cc}
@media(max-width:480px){body{padding:24px 12px}.grid{grid-template-columns:1fr}}
</style><header><h1>MILESTONE NOVA · 화면 미리보기</h1>''',
            f'<p>{html.escape(description)}</p><p>이미지를 누르면 원본 PNG를 엽니다.</p></header><nav>']
markdown = ['# MILESTONE NOVA · 전체 화면 미리보기', '', description, '',
            '[브라우저용 갤러리](index.html)', '']
for index, (group, entries) in enumerate(groups.items()):
    document.append(f'<a href="#group-{index}">{html.escape(group)} · {len(entries)}</a>')
document.append('</nav><main>')
for index, (group, entries) in enumerate(groups.items()):
    document.append(f'<section id="group-{index}"><h2>{html.escape(group)}<span>{len(entries)}개 장면</span></h2><div class="grid">')
    markdown += [f'## {group}', '']
    for number, name, title in entries:
        safe_title, safe_name = html.escape(title), html.escape(name, quote=True)
        document.append(f'<figure><a href="{safe_name}"><img src="{safe_name}" alt="{safe_title}" width="240" height="320" loading="lazy"></a><figcaption><span>{number:02}</span>{safe_title}</figcaption></figure>')
        markdown += [f'### {number:02} · {title}', '', f'![{title}]({name})', '']
    document.append('</div></section>')
document.append('</main></html>')
(folder / 'index.html').write_text('\n'.join(document) + '\n')
(folder / 'index.md').write_text('\n'.join(markdown) + '\n')
print(f"{folder / 'index.html'} ({len(scenes)} named scenes)")
print(folder / 'index.md')
