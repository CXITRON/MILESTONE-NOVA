// Only bounded chunks live in JS memory. IndexedDB survives a tab reload on HTTP APs.
export function crc32(bytes, crc = 0xffffffff) {
  for (const b of bytes) {
    crc ^= b;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (~crc) >>> 0;
}
const CHUNK = 262144;
function result(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}
export class TransferStore {
  async open() {
    const request = indexedDB.open('nova-media-v1', 1);
    request.onupgradeneeded = () => {
      request.result.createObjectStore('chunks');
      request.result.createObjectStore('meta');
    };
    this.db = await result(request);
    return this;
  }
  async transaction(store, mode, fn) {
    const tx = this.db.transaction(store, mode);
    const done = new Promise((resolve, reject) => {
      tx.oncomplete = resolve;
      tx.onabort = () => reject(tx.error || new Error('저장 중단'));
      tx.onerror = () => reject(tx.error);
    });
    let request;
    try {
      request = fn(tx.objectStore(store));
    } catch (error) {
      tx.abort();
      await done.catch(() => {});
      throw error;
    }
    // Observe both failures: a request error also aborts its transaction.
    const [value] = await Promise.all([request ? result(request) : undefined, done]);
    return value;
  }
  async get() {
    return this.transaction('meta', 'readonly', s => s.get('active'));
  }
  async set(meta) {
    return this.transaction('meta', 'readwrite', s => s.put(meta, 'active'));
  }
  async begin(path, name, file) {
    await this.transaction('chunks', 'readwrite', s => s.clear());
    this.meta = {
      id: crypto.getRandomValues(new Uint32Array(1))[0] || 1,
      path,
      name,
      source: file ? await sourceIdentity(file) : null,
      total: 0,
      chunks: 0,
      checksum: 0,
      ready: false
    };
    this.buffer = new Uint8Array(CHUNK);
    this.used = 0;
    await this.set(this.meta);
  }
  async write(data) {
    let at = 0;
    if (this.meta.total + this.used + data.length >= 0x80000000)
      throw new Error('미디어 한도: 2 GiB 미만');
    this.meta.checksum = crc32(data, ~this.meta.checksum);
    while (at < data.length) {
      const n = Math.min(CHUNK - this.used, data.length - at);
      this.buffer.set(data.subarray(at, at + n), this.used);
      this.used += n;
      at += n;
      if (this.used === CHUNK) await this.flush();
    }
  }
  async flush() {
    if (!this.used) return;
    await this.transaction(
        'chunks', 'readwrite', s => s.put(this.buffer.slice(0, this.used), this.meta.chunks));
    this.meta.total += this.used;
    this.meta.chunks++;
    this.used = 0;
  }
  async finish() {
    await this.flush();
    this.meta.ready = true;
    await this.set(this.meta);
    return this.meta;
  }
  async chunk(index) {
    return this.transaction('chunks', 'readonly', s => s.get(index));
  }
  async fromFile(file, path, signal, progress) {
    await this.begin(path, file.name, file);
    for (let at = 0; at < file.size; at += CHUNK) {
      signal.throwIfAborted();
      await this.write(new Uint8Array(await file.slice(at, at + CHUNK).arrayBuffer()));
      progress((at + CHUNK) / file.size);
    }
    return this.finish();
  }
}
export async function uploadPrepared(store, api, signal, progress, committing, retried) {
  signal.throwIfAborted();
  const meta = await store.get();
  if (!meta?.ready) throw new Error('먼저 파일을 변환·준비하세요.');
  signal.throwIfAborted();
  const begun = await api('/api/transfer', {op: 'begin', ...meta, replace: true}, signal);
  let offset = begun.offset;
  if (!Number.isSafeInteger(offset) || offset < 0 || offset > meta.total)
    throw new Error('기기와 브라우저의 전송 크기가 다릅니다.');
  while (offset < meta.total) {
    signal.throwIfAborted();
    const data = await store.chunk(Math.floor(offset / CHUNK));
    if (!data) throw new Error('브라우저의 전송 자료가 없습니다. 다시 변환하세요.');
    const part = data.subarray(offset % CHUNK);
    if (!part.length || offset + part.length > meta.total)
      throw new Error('브라우저의 전송 자료가 손상되었습니다. 다시 변환하세요.');
    const query = new URLSearchParams({id: meta.id, offset, crc: crc32(part)});
    const body = new FormData();
    body.append('chunk', new Blob([part]), 'chunk.bin');
    // A chunk is safe to send again: the device checks an already written range against the CRC.
    // Retry transient failures (busy SD, lost response, brief Wi-Fi stall) instead of aborting.
    let next;
    for (let attempt = 0;; ++attempt) {
      try {
        next = await api('/api/blob?' + query, body, signal);
        break;
      } catch (error) {
        signal.throwIfAborted();
        if (attempt >= 3 || /CRC|Invalid chunk|Wrong|Incomplete/.test(error.message)) throw error;
        retried?.(attempt + 1, error.message);
        await new Promise(resolve => setTimeout(resolve, 700 * (attempt + 1)));
      }
    }
    if (next.offset !== offset + part.length) throw new Error('전송 체크포인트 불일치');
    offset = next.offset;
    progress(offset / meta.total);
  }
  signal.throwIfAborted();
  // The device validates in the background; poll its progress instead of waiting on one request.
  committing?.(0);
  let state = await api('/api/transfer', {op: 'commit', id: meta.id}, signal);
  while (state.state === 'running') {
    await new Promise(resolve => setTimeout(resolve, 700));
    signal.throwIfAborted();
    try {
      state = await api('/api/transfer', {op: 'commitStatus'}, signal);
    } catch (error) {
      signal.throwIfAborted();
      if (/AP session|Invalid|검증|Frame|CRC|Validation|header|Incomplete/i.test(error.message)) throw error;
      state = {state: 'running', progress: state.progress};
      continue;
    }
    committing?.(state.progress);
  }
  if (state.state !== 'done') throw new Error(state.message || '기기의 영상 검증에 실패했습니다.');
  return meta;
}
export class SyncClock {
  constructor(audio, send, onStatus) {
    this.audio = audio;
    this.send = send;
    this.onStatus = onStatus;
    this.session = 0;
    this.sequence = 0;
    this.busy = false;
    this.latency = 0;
    this.timer = 0;
    this.lifecycle = Promise.resolve();
  }
  transition(fn) {
    const next = this.lifecycle.then(fn);
    this.lifecycle = next.catch(() => {});
    return next;
  }
  start(session, media) {
    return this.transition(async () => {
      if (!media?.ready || !Number.isInteger(media.checksum))
        throw new Error('동기 영상의 검증 정보가 없습니다. 다시 준비하세요.');
      await this.detach();
      await this.send({op: 'syncStart', session, size: media.total, crc: media.checksum});
      this.session = session;
      this.sequence = 0;
      this.timer = setInterval(() => this.tick(), 250);
      await this.tick();
    });
  }
  async tick() {
    if (!this.session || this.busy) return;
    this.busy = true;
    const session = this.session, start = performance.now();
    try {
      await this.send({
        op: 'sync',
        session,
        sequence: ++this.sequence,
        position: Math.round(
            (this.audio.currentTime + (!this.audio.paused ? this.latency / 2000 : 0)) * 1000),
        playing: !this.audio.paused && !this.audio.ended
      });
      this.latency = Math.min(500, this.latency * .8 + (performance.now() - start) * .2);
      this.onStatus('SD 영상 ↔ 브라우저 오디오 연결됨');
    } catch (error) {
      this.audio.pause();
      this.onStatus(error.message);
    } finally {
      this.busy = false;
    }
  }
  stop() {
    return this.transition(() => this.detach());
  }
  async detach() {
    clearInterval(this.timer);
    this.audio.pause();
    const active = this.session;
    this.session = 0;
    // Wait for an in-flight tick so a late control can never restart a stopped session.
    while (this.busy) await new Promise(resolve => setTimeout(resolve, 20));
    if (active) await this.send({op: 'syncStop', session: active});
    this.onStatus('동기 연결 종료. 준비된 영상으로 다시 연결할 수 있습니다.');
  }
}

// Size, timestamp and bounded content samples catch accidental same-name originals
// after a tab reload without retaining the entire video in memory.
export async function sourceIdentity(file) {
  return {
    size: file.size,
    modified: file.lastModified,
    head: crc32(new Uint8Array(await file.slice(0, 65536).arrayBuffer())),
    tail: crc32(new Uint8Array(await file.slice(Math.max(0, file.size - 65536)).arrayBuffer()))
  };
}
export async function matchesSource(meta, file) {
  if (!meta?.ready || meta.name !== file.name || !meta.source) return false;
  const actual = await sourceIdentity(file);
  return Object.keys(actual).every(key => actual[key] === meta.source[key]);
}
