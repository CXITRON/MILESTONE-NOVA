import assert from 'node:assert/strict';

import {decodeLzw} from '../assets/portal/gif.js';
import {crc32, matchesSource, sourceIdentity, SyncClock, TransferStore, uploadPrepared} from '../assets/portal/transfer.js';

assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
assert.deepEqual([...decodeLzw(Uint8Array.of(0x44, 0x01), 2, 1)], [0]);
assert.throws(() => decodeLzw(Uint8Array.of(0x44), 2, 2));
assert.throws(() => decodeLzw(Uint8Array.of(0xff, 0xff), 2, 1));
assert.throws(() => decodeLzw(new Uint8Array(), 9, 1));
// Resume halfway through a persisted chunk; server offsets must be exact.
const bytes = new Uint8Array(262144 + 100).map((_, i) => i % 251), meta = {
  id: 4,
  total: bytes.length,
  path: '/media/test.njv',
  name: 'test',
  ready: true
};
const store = {
  get: async () => meta,
  chunk: async i => bytes.slice(i * 262144, (i + 1) * 262144)
};
let offset = 10000, committed = false;
await uploadPrepared(store, async (url, body) => {
  if (url === '/api/transfer') {
    if (body.op === 'begin') return {offset};
    if (body.op === 'commit') return {ok: true, state: 'running', progress: 0};
    if (body.op === 'commitStatus') {
      committed = true;
      return {ok: true, state: 'done', progress: 100};
    }
  }
  const q = new URL(url, 'http://device/').searchParams,
        data = new Uint8Array(await body.get('chunk').arrayBuffer());
  assert.equal(Number(q.get('offset')), offset);
  assert.equal(Number(q.get('crc')), crc32(data));
  assert.deepEqual(data, bytes.slice(offset, offset + data.length));
  offset += data.length;
  return {offset};
}, new AbortController().signal, () => {});
assert.equal(offset, bytes.length);
assert(committed);
await assert.rejects(
    uploadPrepared(
        store, async (url) => url === '/api/transfer' ? {offset: 0} : {offset: 1},
        new AbortController().signal, () => {}),
    /체크포인트/);
const aborted = new AbortController();
aborted.abort(new Error('cancel'));
await assert.rejects(
    uploadPrepared(store, async () => ({offset: 0}), aborted.signal, () => {}), /cancel/);
let requests = 0;
await assert.rejects(uploadPrepared(store, async () => {
                       requests++;
                     }, aborted.signal, () => {}), /cancel/);
assert.equal(requests, 0, 'An already cancelled transfer must not replace the device checkpoint');
for (const invalid of [undefined, -1, 1.5, NaN, bytes.length + 1]) {
  requests = 0;
  await assert.rejects(uploadPrepared(store, async () => {
                         requests++;
                         return {offset: invalid};
                       }, new AbortController().signal, () => {}), /전송 크기/);
  assert.equal(requests, 1, 'Invalid offsets must never reach commit');
}
const beginAbort = new AbortController();
await assert.rejects(uploadPrepared(store, async (url, body, signal) => {
                       assert.equal(signal, beginAbort.signal);
                       beginAbort.abort(new Error('begin-cancelled'));
                       signal.throwIfAborted();
                     }, beginAbort.signal, () => {}), /begin-cancelled/);
await assert.rejects(
    uploadPrepared(
        {...store, chunk: async () => new Uint8Array()}, async () => ({offset: 0}),
        new AbortController().signal, () => {}),
    /손상/);

// Exercise the actual bounded writer across chunk boundaries and arbitrary write sizes.
const persisted = {
  chunks: new Map(),
  meta: new Map()
},
      preparedStore = new TransferStore();
preparedStore.transaction = async (name, mode, fn) => fn({
  clear: () => persisted[name].clear(),
  put: (value, key) => persisted[name].set(key, structuredClone(value)),
  get: key => persisted[name].get(key)
});
await preparedStore.begin('/media/test.njv', 'test');
await preparedStore.write(bytes.subarray(0, 123));
await preparedStore.write(bytes.subarray(123));
const finished = await preparedStore.finish();
assert.equal(finished.total, bytes.length);
assert.equal(finished.checksum, crc32(bytes));
assert.deepEqual(await preparedStore.chunk(0), bytes.subarray(0, 262144));
assert.deepEqual(await preparedStore.chunk(1), bytes.subarray(262144));

// New raw requests combine persisted chunks without changing their layout. Resume can
// cross three stored blocks; a lost acknowledgement must retry exactly the same bytes.
const rawBytes = Uint8Array.from({length: 3 * 262144 + 137}, (_, i) => (i * 17) & 255);
for (const capacity of [262144, 524288]) {
  let durable = 10000, lostReply = false, retriedRaw = false, commits = 0;
  const rawStore = {
    get: async () => ({...meta, total: rawBytes.length}),
    chunk: async i => rawBytes.slice(i * 262144, (i + 1) * 262144)
  };
  await uploadPrepared(rawStore, async (url, body) => {
    if (url === '/api/transfer') {
      if (body.op === 'begin') return {offset: durable, rawChunks: true, chunkBytes: capacity};
      assert.equal(body.op, 'commit'); ++commits; return {state: 'done'};
    }
    assert(url.startsWith('/api/blob/raw?'));
    assert(body instanceof Blob);
    const q = new URL(url, 'http://device/').searchParams;
    const start = Number(q.get('offset')), data = new Uint8Array(await body.arrayBuffer());
    assert.equal(data.length, Math.min(capacity, rawBytes.length - start));
    assert.equal(Number(q.get('crc')), crc32(data));
    assert.deepEqual(data, rawBytes.subarray(start, start + data.length));
    if (start === durable) durable += data.length;
    else { assert.equal(start + data.length, durable); retriedRaw = true; }
    if (!lostReply) { lostReply = true; throw new Error('lost acknowledgement'); }
    return {offset: durable};
  }, new AbortController().signal, () => {});
  assert.equal(durable, rawBytes.length); assert(retriedRaw); assert.equal(commits, 1);
}
for (const capacity of [0, 524289, NaN, 262144.5]) {
  let requests = 0;
  await assert.rejects(uploadPrepared(store, async () => {
    ++requests; return {offset: 0, rawChunks: true, chunkBytes: capacity};
  }, new AbortController().signal, () => {}), /전송 단위/);
  assert.equal(requests, 1);
}

// IndexedDB rejects both the request and the transaction for a quota/write failure.
const failedStore = new TransferStore(), unhandled = [];
const unhandledListener = error => unhandled.push(error);
process.on('unhandledRejection', unhandledListener);
failedStore.db = {
  transaction() {
    const request = {}, tx = {objectStore: () => ({put: () => request})};
    queueMicrotask(() => {
      request.error = tx.error = new Error('quota-test');
      request.onerror();
      tx.onabort();
    });
    return tx;
  }
};
await assert.rejects(failedStore.set({ready: true}), /quota-test/);
await new Promise(resolve => setImmediate(resolve));
process.removeListener('unhandledRejection', unhandledListener);
assert.deepEqual(unhandled, []);

const calls = [], audio = {
  currentTime: 1,
  paused: true,
  ended: false,
  pause() {
    this.paused = true;
  }
};
const clock = new SyncClock(audio, async x => {
  calls.push(x);
}, () => {});
const syncMedia = {
  ready: true,
  total: 1024,
  checksum: 0x12345678
};
await clock.start(31, syncMedia);
assert.deepEqual(calls[0], {op: 'syncStart', session: 31, size: 1024, crc: 0x12345678});
audio.paused = false;
audio.currentTime = 4;
await clock.tick();
assert(calls.some(c => c.op === 'sync' && c.position >= 4000 && c.playing));
await clock.stop();
assert.equal(calls.at(-1).op, 'syncStop');
assert.equal(calls.at(-1).session, 31);
const count = calls.length;
await clock.tick();
assert.equal(calls.length, count);
// Stop waits for in-flight controls; timeout failure pauses browser audio.
let release;
clock.send = async c => {
  if (c.op === 'sync') await new Promise(r => release = r);
  calls.push(c);
};
clock.session = 33;
audio.paused = false;
const tick = clock.tick(), stop = clock.stop();
release();
await tick;
await stop;
assert.equal(calls.at(-1).op, 'syncStop');
clock.send = async () => {
  throw new Error('offline');
};
clock.session = 34;
audio.paused = false;
await clock.tick();
assert(audio.paused);
clock.session = 0;

// A second start and stop are serialized behind a slow first start.
const transitions = [];
let acknowledge;
clock.send = async c => {
  transitions.push(c);
  if (c.op === 'syncStart' && c.session === 40) await new Promise(resolve => acknowledge = resolve);
};
const firstStart = clock.start(40, syncMedia);
const secondStart = clock.start(41, syncMedia);
const finalStop = clock.stop();
await new Promise(resolve => setImmediate(resolve));
assert.deepEqual(transitions.map(c => c.op), ['syncStart']);
acknowledge();
await Promise.all([firstStart, secondStart, finalStop]);
assert.deepEqual(
    transitions.filter(c => c.op !== 'sync').map(c => [c.op, c.session]),
    [['syncStart', 40], ['syncStop', 40], ['syncStart', 41], ['syncStop', 41]]);
assert.equal(clock.session, 0);
await assert.rejects(clock.start(42, {ready: true}), /검증 정보/);

// Long SD validation is polled without sending clock ticks or starting audio early.
const preparationCalls = [], progressMessages = [];
let validations = 0;
const preparing = new SyncClock(audio, async c => {
  preparationCalls.push(c);
  if (c.op === 'syncStart' && ++validations < 3)
    return {ok: true, state: 'running', progress: validations * 30};
  return {ok: true};
}, text => progressMessages.push(text));
await preparing.start(51, syncMedia);
assert.deepEqual(preparationCalls.slice(0, 3).map(c => c.op), ['syncStart', 'syncStart', 'syncStart']);
assert.equal(preparationCalls[3].op, 'sync');
assert(progressMessages.some(s => s.includes('30%')));
assert(audio.paused);
await preparing.stop();
// Stop while validation is pending must cancel it and never activate a local session.
let resolvePreparation;
const cancelledCalls = [];
const cancelling = new SyncClock(audio, async c => {
  cancelledCalls.push(c);
  if (c.op === 'syncStart') return new Promise(resolve => resolvePreparation = resolve);
  return {ok: true};
}, () => {});
const pendingStart = cancelling.start(52, syncMedia);
const rejectedStart = assert.rejects(pendingStart, /취소/);
await new Promise(resolve => setImmediate(resolve));
const cancel = cancelling.stop();
resolvePreparation({ok: true, state: 'running', progress: 5});
await Promise.all([rejectedStart, cancel]);
assert.equal(cancelling.session, 0);
assert(cancelledCalls.some(c => c.op === 'syncCancel' && c.session === 52));
assert(!cancelledCalls.some(c => c.op === 'sync'));
// If the final activation response arrives after Stop, explicitly stop the accepted session.
let resolveActivation;
const lateCalls = [];
const late = new SyncClock(audio, async c => {
  lateCalls.push(c);
  if (c.op === 'syncStart') return new Promise(resolve => resolveActivation = resolve);
  return {ok: true};
}, () => {});
const lateStart = late.start(53, syncMedia);
const lateRejected = assert.rejects(lateStart, /취소/);
await new Promise(resolve => setImmediate(resolve));
const lateStop = late.stop();
resolveActivation({ok: true});
await Promise.all([lateRejected, lateStop]);
assert(lateCalls.some(c => c.op === 'syncStop' && c.session === 53));
assert.equal(late.session, 0);

const original = new File([new Uint8Array([1, 2, 3])], 'movie.mp4', {lastModified: 123});
const prepared = {
  name: original.name,
  ready: true,
  source: await sourceIdentity(original)
};
assert(await matchesSource(prepared, original));
assert(!await matchesSource(
    prepared, new File([new Uint8Array([1, 2, 4])], 'movie.mp4', {lastModified: 123})));
assert(!await matchesSource(
    prepared, new File([new Uint8Array([1, 2, 3])], 'movie.mp4', {lastModified: 124})));
console.log(
    'Portal CRC, bounded writer, failed transactions, resume/abort/invalid offsets, GIF, source identity and serialized Sync passed');

// A trailing // comment once swallowed the rest of the call (`signal,`) and broke conversion on the
// device while `node --check` stayed green. Pin the arguments that must reach convert().
{
  const source = await (await import('node:fs/promises')).readFile(new URL('../assets/portal/app.js', import.meta.url), 'utf8');
  const call = source.match(/await convert\(file, store,\s*\{path: outputPath\(file, true\)[^}]*\}\)/s);
  assert(call, 'sync convert call not found');
  assert(/fps: syncFps, quality: syncQuality, signal,/.test(call[0]), 'sync convert call must pass fps, quality and signal');
  for (const line of source.split('\n'))
    if (/\bsignal,\s*$/.test(line) === false && /\/\/.*\bsignal,/.test(line))
      assert.fail('a line comment swallows "signal," here: ' + line.trim());
}
