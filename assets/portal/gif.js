// GIF87a/89a LZW, interlace, transparency, and disposal 2/3. No network dependency.
export function decodeLzw(bytes, minCodeSize, count) {
  if (minCodeSize < 2 || minCodeSize > 8) throw new Error('GIF LZW 크기 오류');
  const out = new Uint8Array(count), prefix = new Uint16Array(4096), suffix = new Uint8Array(4096),
        stack = new Uint8Array(4097);
  const clear = 1 << minCodeSize, end = clear + 1;
  let next = end + 1, bits = minCodeSize + 1, bit = 0, old = -1, first = 0, at = 0, ended = false;
  const code = () => {
    if (bit + bits > bytes.length * 8) return -1;
    let n = 0;
    for (let b = 0; b < bits; b++) n |= ((bytes[(bit + b) >> 3] >> ((bit + b) & 7)) & 1) << b;
    bit += bits;
    return n;
  };
  for (let i = 0; i < clear; i++) suffix[i] = i;
  for (;;) {
    let c = code();
    if (c < 0) break;
    if (c === clear) {
      next = end + 1;
      bits = minCodeSize + 1;
      old = -1;
      continue;
    }
    if (c === end) {
      ended = true;
      break;
    }
    if (c > next || c >= 4096) throw new Error('GIF 사전 오류');
    const original = c;
    let top = 0;
    if (c === next) {
      if (old < 0) throw new Error('GIF 초기 코드 오류');
      stack[top++] = first;
      c = old;
    }
    while (c >= clear) {
      if (c >= next || top >= 4096) throw new Error('GIF 순환 코드');
      stack[top++] = suffix[c];
      c = prefix[c];
    }
    first = suffix[c];
    stack[top++] = first;
    if (at + top > count) throw new Error('GIF 픽셀 초과');
    while (top) out[at++] = stack[--top];
    if (old >= 0 && next < 4096) {
      prefix[next] = old;
      suffix[next++] = first;
      if (next === (1 << bits) && bits < 12) bits++;
    }
    old = original;
  }
  if (!ended || at !== count) throw new Error('GIF 픽셀 길이 오류');
  return out;
}
export async function gifFrames(file, signal) {
  if (file.size > 64 * 1024 * 1024) throw new Error('GIF 원본 한도: 64 MiB');
  const b = new Uint8Array(await file.arrayBuffer()), v = new DataView(b.buffer);
  let p = 0;
  const need = n => {
    if (p + n > b.length) throw new Error('잘린 GIF 파일');
  };
  const byte = () => {
    need(1);
    return b[p++];
  }, word = () => {
    need(2);
    const n = v.getUint16(p, true);
    p += 2;
    return n;
  };
  const table = n => {
    need(n * 3);
    const out = b.subarray(p, p + n * 3);
    p += n * 3;
    return out;
  };
  const blocks = () => {
    const list = [];
    let total = 0;
    for (;;) {
      const n = byte();
      if (!n) break;
      need(n);
      list.push(b.subarray(p, p + n));
      p += n;
      total += n;
    }
    return {list, total};
  };
  need(13);
  const magic = String.fromCharCode(...b.subarray(0, 6));
  if (magic !== 'GIF87a' && magic !== 'GIF89a') throw new Error('GIF 서명 오류');
  p = 6;
  const width = word(), height = word(), flags = byte();
  byte();
  byte();
  if (!width || !height || width > 2048 || height > 2048)
    throw new Error('GIF 해상도 한도: 2048 × 2048');
  const global = flags & 128 ? table(2 << (flags & 7)) : null, frames = [];
  let control = {delay: 100, disposal: 0, transparent: -1}, duration = 0;
  while (p < b.length) {
    signal.throwIfAborted();
    const kind = byte();
    if (kind === 0x3b) break;
    if (kind === 0x21) {
      const extension = byte();
      if (extension === 0xf9) {
        if (byte() !== 4) throw new Error('GIF 제어 블록 오류');
        const packed = byte(), delay = word() * 10, transparent = byte();
        if (byte() !== 0) throw new Error('GIF 제어 종료 오류');
        control = {
          delay: Math.max(20, delay || 100),
          disposal: (packed >> 2) & 7,
          transparent: packed & 1 ? transparent : -1
        };
      } else
        blocks();
      continue;
    }
    if (kind !== 0x2c) throw new Error('GIF 블록 오류');
    const x = word(), y = word(), w = word(), h = word(), packed = byte();
    if (!w || !h || x + w > width || y + h > height || control.disposal > 3)
      throw new Error('GIF 프레임 영역/처리 오류');
    const colors = packed & 128 ? table(2 << (packed & 7)) : global;
    if (!colors) throw new Error('GIF 색상표 누락');
    const minimum = byte(), data = blocks();
    frames.push({x, y, w, h, interlace: !!(packed & 64), colors, minimum, data, ...control});
    duration += control.delay;
    control = {delay: 100, disposal: 0, transparent: -1};
    if (frames.length > 4096 || duration > 21600000) throw new Error('GIF 길이 한도 초과');
  }
  if (!frames.length) throw new Error('GIF 프레임 없음');
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d', {willReadFrequently: true});
  let index = 0, previous = null, restore = null;
  return {
    duration,
    async next() {
      signal.throwIfAborted();
      if (index >= frames.length) throw new Error('GIF 프레임 종료');
      if (previous?.disposal === 2)
        ctx.clearRect(previous.x, previous.y, previous.w, previous.h);
      else if (previous?.disposal === 3 && restore)
        ctx.putImageData(restore, previous.x, previous.y);
      const f = frames[index++];
      restore = f.disposal === 3 ? ctx.getImageData(f.x, f.y, f.w, f.h) : null;
      const compressed = new Uint8Array(f.data.total);
      let at = 0;
      for (const block of f.data.list) {
        compressed.set(block, at);
        at += block.length;
      }
      const pixels = decodeLzw(compressed, f.minimum, f.w * f.h),
            image = ctx.getImageData(f.x, f.y, f.w, f.h), rows = [];
      if (f.interlace) {
        for (const [start, step] of [[0, 8], [4, 8], [2, 4], [1, 2]])
          for (let y = start; y < f.h; y += step) rows.push(y);
      } else
        for (let y = 0; y < f.h; y++) rows.push(y);
      for (let row = 0; row < f.h; row++)
        for (let x = 0; x < f.w; x++) {
          const color = pixels[row * f.w + x];
          if (color === f.transparent) continue;
          if (color * 3 + 2 >= f.colors.length) throw new Error('GIF 색상 인덱스 오류');
          const offset = (rows[row] * f.w + x) * 4;
          image.data.set(f.colors.subarray(color * 3, color * 3 + 3), offset);
          image.data[offset + 3] = 255;
        }
      ctx.putImageData(image, f.x, f.y);
      previous = f;
      await new Promise(resolve => setTimeout(resolve, 0));
      return {canvas, delay: f.delay};
    }
  };
}
