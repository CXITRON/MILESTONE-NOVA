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
    if (body.op === 'commit') {
      committed = true;
      return {ok: true};
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
