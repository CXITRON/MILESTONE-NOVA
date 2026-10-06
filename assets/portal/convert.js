import {gifFrames} from './gif.js';
import {crc32} from './transfer.js';

// Device pixel sides: full-width local media and the NOW artwork cache.
export const MEDIA_SIDE = 240, ART_SIDE = 200;
const MAX_FRAME = 49152;
export function imageBytes(image, canvas = document.createElement('canvas'), SIDE = MEDIA_SIDE) {
  canvas.width = canvas.height = SIDE;
  const ctx = canvas.getContext('2d', {willReadFrequently: true});
  const w = image.videoWidth || image.naturalWidth || image.width,
        h = image.videoHeight || image.naturalHeight || image.height;
  const scale = Math.max(SIDE / w, SIDE / h);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, SIDE, SIDE);
  ctx.drawImage(image, (SIDE - w * scale) / 2, (SIDE - h * scale) / 2, w * scale, h * scale);
  const payload = SIDE * SIDE * 2, rgba = ctx.getImageData(0, 0, SIDE, SIDE).data,
        out = new Uint8Array(16 + payload), view = new DataView(out.buffer);
  out.set(new TextEncoder().encode('NVI1'));
  view.setUint16(4, SIDE, true);
  view.setUint16(6, SIDE, true);
  view.setUint32(8, payload, true);
  for (let i = 0; i < SIDE * SIDE; i++)
    view.setUint16(
        16 + i * 2,
        ((rgba[i * 4] >> 3) << 11) | ((rgba[i * 4 + 1] >> 2) << 5) | (rgba[i * 4 + 2] >> 3), true);
  view.setUint32(12, crc32(out.subarray(16)), true);
  return out;
}
function event(target, name, signal, timeout = 20000) {
  return new Promise((resolve, reject) => {
    let timer;
    const done = (error) => {
      clearTimeout(timer);
      target.removeEventListener(name, ready);
      target.removeEventListener('error', failed);
      signal?.removeEventListener('abort', aborted);
      error ? reject(error) : resolve();
    };
    const ready = () => done(),
          failed = () => done(new Error('브라우저가 이 파일을 해석하지 못했습니다.')),
          aborted = () => done(signal.reason || new Error('중단'));
    target.addEventListener(name, ready, {once: true});
    target.addEventListener('error', failed, {once: true});
    signal?.addEventListener('abort', aborted, {once: true});
    timer = setTimeout(() => done(new Error('프레임 디코딩 시간 초과')), timeout);
    if (signal?.aborted) aborted();
  });
}
export async function loadImage(file, signal) {
  const image = new Image(), url = URL.createObjectURL(file), loaded = event(image, 'load', signal);
  image.src = url;
  try {
    await loaded;
    return image;
  } finally {
    URL.revokeObjectURL(url);
  }
}
function header(frames, fps) {
  const b = new Uint8Array(16), v = new DataView(b.buffer);
  b.set(new TextEncoder().encode('NJV1'));
  v.setUint16(4, MEDIA_SIDE, true);
  v.setUint16(6, MEDIA_SIDE, true);
  v.setUint16(8, fps, true);
  v.setUint32(12, frames, true);
  return b;
}
async function jpeg(canvas, quality = .8) {
  let blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', quality));
  if (!blob || blob.size > MAX_FRAME)
    blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', Math.min(quality, .55)));
  if (!blob || blob.size > MAX_FRAME) throw new Error('JPEG 프레임이 허용 크기를 초과했습니다.');
  const pixels = new Uint8Array(await blob.arrayBuffer()), out = new Uint8Array(pixels.length + 8),
        v = new DataView(out.buffer);
  v.setUint32(0, pixels.length, true);
  v.setUint32(4, crc32(pixels), true);
  out.set(pixels, 8);
  return out;
}
export async function convert(file, store, {path, fps = 20, quality = .8, signal, progress}) {
  if (!file) throw new Error('파일을 선택하세요.');
  if (/\.(nvi|nvv|njv|mvj|msm|bmp)$/i.test(file.name))
    return store.fromFile(file, path, signal, progress);
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = MEDIA_SIDE;
  if (file.type === 'image/gif' || /\.gif$/i.test(file.name)) {
    const gif = await gifFrames(file, signal);
    fps = 20;
    const count = Math.ceil(gif.duration * fps / 1000);
    await store.begin(path, file.name, file);
    await store.write(header(count, fps));
    let next = await gif.next(), end = next.delay;
    for (let i = 0; i < count; i++) {
      signal.throwIfAborted();
      while (i * 1000 / fps >= end) {
        next = await gif.next();
        end += next.delay;
      }
      imageBytes(next.canvas, canvas);
      await store.write(await jpeg(canvas, quality));
      progress((i + 1) / count);
    }
    return store.finish();
  }
  if ((file.type.startsWith('image/') || /\.(png|jpe?g|webp|avif|heic|heif)$/i.test(file.name))) {
    const image = await loadImage(file, signal);
    await store.begin(path, file.name, file);
    await store.write(imageBytes(image, canvas));
    progress(1);
    return store.finish();
  }
  const video = document.createElement('video');
  video.muted = true;
  video.preload = 'auto';
  video.playsInline = true;
  const url = URL.createObjectURL(file), loaded = event(video, 'loadeddata', signal);
  video.src = url;
  try {
    await loaded;
    if (!Number.isFinite(video.duration) || video.duration <= 0 || video.duration > 21600)
      throw new Error('영상 길이는 0초 초과, 6시간 이하여야 합니다.');
    const count = Math.ceil(video.duration * fps);
    await store.begin(path, file.name, file);
    await store.write(header(count, fps));
    for (let i = 0; i < count; i++) {
      signal.throwIfAborted();
      const time = Math.min(i / fps, video.duration - .001);
      if (Math.abs(video.currentTime - time) > .0001) {
        const seeked = event(video, 'seeked', signal);
        video.currentTime = time;
        await seeked;
      }
      imageBytes(video, canvas);
      await store.write(await jpeg(canvas, quality));
      progress((i + 1) / count);
    }
    return store.finish();
  } finally {
    video.removeAttribute('src');
    video.load();
    URL.revokeObjectURL(url);
  }
}
export function drawNvi(data, canvas) {
  const v = new DataView(data), b = new Uint8Array(data),
        SIDE = data.byteLength >= 16 ? v.getUint16(4, true) : 0;
  if (![160, ART_SIDE, MEDIA_SIDE].includes(SIDE) || v.getUint16(6, true) !== SIDE ||
      data.byteLength !== 16 + SIDE * SIDE * 2)
    throw new Error('잘못된 NVI 이미지');
  if (String.fromCharCode(...b.subarray(0, 4)) !== 'NVI1' ||
      crc32(b.subarray(16)) !== v.getUint32(12, true))
    throw new Error('이미지 CRC 오류');
  canvas.width = canvas.height = SIDE;
  const ctx = canvas.getContext('2d'), im = ctx.createImageData(SIDE, SIDE);
  for (let i = 0; i < SIDE * SIDE; i++) {
    const c = v.getUint16(16 + i * 2, true);
    im.data[i * 4] = Math.round((c >> 11) * 255 / 31);
    im.data[i * 4 + 1] = Math.round(((c >> 5) & 63) * 255 / 63);
    im.data[i * 4 + 2] = Math.round((c & 31) * 255 / 31);
    im.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(im, 0, 0);
}
