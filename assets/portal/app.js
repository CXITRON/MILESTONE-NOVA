import {ART_SIDE, convert, drawNvi, imageBytes, loadImage} from './convert.js';
import {matchesSource, SyncClock, TransferStore, uploadPrepared} from './transfer.js';

const $ = s => document.querySelector(s), all = s => [...document.querySelectorAll(s)];
let token = '', latest = null, busy = false, controller = null, toastTimer, previewUrl = '',
    syncUrl = '';
let store;
try {
  store = await new TransferStore().open();
} catch (error) {
  toast(
      '브라우저 저장소를 열 수 없습니다. 미디어 전송은 일반 브라우저에서 다시 시도하세요. 설정은 계속 사용할 수 있습니다.');
}
function toast(message) {
  $('#toast').textContent = message;
  $('#toast').style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('#toast').style.display = 'none', 6500);
}
async function api(path, data, signal) {
  const deadline = AbortSignal.timeout(data?.op === 'sync' ? 1800 : 300000);
  const options = {
    signal: signal ? AbortSignal.any([signal, deadline]) : deadline,
    cache: 'no-store',
    headers: {'X-NOVA': token}
  };
  if (data !== undefined) {
    options.method = 'POST';
    if (data instanceof FormData)
      options.body = data;
    else {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(data);
    }
  }
  const response = await fetch(path, options);
  const type = response.headers.get('Content-Type') || '';
  if (!type.includes('application/json')) {
    if (!response.ok) throw new Error('요청 실패: ' + response.status);
    return response;
  }
  const body = await response.json();
  if (!response.ok || body.ok === false)
    throw new Error(body.message || '기기가 요청을 처리하지 못했습니다.');
  return body;
}
const action = data => api('/api/action', data), run = fn => async event => {
  try {
    await fn(event);
  } catch (error) {
    toast(error.message || String(error));
  }
};
function element(tag, text, className) {
  const e = document.createElement(tag);
  if (text !== undefined) e.textContent = text;
  if (className) e.className = className;
  return e;
}
function button(text, fn) {
  const b = element('button', text);
  b.onclick = run(fn);
  return b;
}
all('nav button').forEach(b => b.onclick = run(async () => {
                            all('.page').forEach(p => p.hidden = p.id !== b.dataset.page);
                            all('nav button').forEach(n => n.classList.toggle('active', n === b));
                            if (b.dataset.page === 'media') await mediaList();
                            if (b.dataset.page === 'settings' &&
                                !$('#setting-list').childNodes.length)
                              await settingsList();
                          }));
all('[data-mode]').forEach(b => b.onclick = run(async () => {
                             await action({op: 'mode', value: Number(b.dataset.mode)});
                             toast('모드 선택을 저장했습니다. 기기 BACK으로 AP를 닫으세요.');
                           }));
all('[data-action]').forEach(b => b.onclick = run(async () => {
                               await action({op: b.dataset.action});
                               toast('요청을 적용했습니다.');
                             }));
async function poll() {
  try {
    const s = await api('/api/status');
    latest = s;
    token = s.token;
    $('#connection').textContent =
        `${s.network.connected ? 'Wi-Fi 연결됨' : '설정 AP'} · ${s.sd ? 'SD 준비됨' : 'SD 없음'}`;
    $('#device-message').textContent = s.message;
    $('#diagnostics').textContent = s.diagnostics;
    $('#wifi-test').textContent = s.network.test;
    const overview = $('#overview');
    overview.replaceChildren();
    for (const [name, value] of [
             ['모드', ['CORE', 'MEDIA', 'NOW'][s.mode]],
             [
               '환경',
               s.sensor ? `${s.temperature.toFixed(1)} °C / ${s.humidity.toFixed(1)} %` :
                          '센서 대기'
             ],
             ['배터리', s.volts.toFixed(2) + ' V'], ['가동 시간', Math.floor(s.uptime / 60) + '분'],
             ['BLE', s.ble]])
      overview.append(element('dt', name), element('dd', value));
    if (!$('#art-query [name=key]').value && s.track.key)
      for (const key of ['key', 'title', 'artist', 'album'])
        $('#art-query [name=' + key + ']').value = s.track[key];
    const scan = $('#scan-list');
    scan.replaceChildren();
    for (const item of s.network.scan) {
      const b = button(
          `${item.ssid} · ${item.rssi} dBm${item.supported ? '' : ' · 지원되지 않는 보안'}`, () => {
            $('#wifi-form [name=ssid]').value = item.ssid;
          });
      b.disabled = !item.supported;
      scan.append(b);
    }
    const saved = $('#saved-networks');
    saved.replaceChildren();
    s.network.saved.forEach((name, index) => {
      const row = element('div', undefined, 'card');
      row.append(
          element('span', name, 'name'),
          button('시험 연결', () => action({op: 'wifiUse', value: index})),
          button('삭제', async () => {
            if (confirm(name + ' 프로필을 삭제할까요?'))
              await action({op: 'wifiDelete', value: index});
          }));
      saved.append(row);
    });
    if (clock.session && (s.syncStale || s.syncSession !== clock.session)) {
      $('#audio').pause();
      $('#sync-state').textContent = '기기에서 동기가 중지되었습니다. 다시 연결하세요.';
    }
  } catch (error) {
    $('#connection').textContent = '기기 연결 확인 필요';
  }
  setTimeout(poll, busy ? 1500 : 2500);
}
$('#set-time').onclick = run(() => action({op: 'time', epoch: Math.floor(Date.now() / 1000)}));
$('#wifi-form').onsubmit = run(async e => {
  e.preventDefault();
  const values = Object.fromEntries(new FormData(e.target));
  await action({op: 'wifi', ...values, auth: Number(values.auth)});
  e.target.password.value = '';
  toast('시험 연결 중입니다. 결과를 확인하세요.');
});
$('#ap-form').onsubmit = run(async e => {
  e.preventDefault();
  const values = Object.fromEntries(new FormData(e.target)), mode = Number(values.mode);
  if (mode === 2 &&
      !confirm('암호 없이 주변 기기가 설정 AP에 접속할 수 있습니다. 개방형 AP를 사용할까요?'))
    return;
  await action({op: 'ap', ...values, mode, confirmed: mode === 2});
  e.target.password.value = '';
  toast('다음 AP부터 적용됩니다.');
});
const labels = {
  lcd_brightness: 'LCD 밝기',
  rgb_brightness: 'RGB 밝기',
  heartbeat_brightness: '상태 LED 밝기',
  message: '메시지',
  label: '기기 이름',
  temperature_offset: '온도 보정',
  humidity_offset: '습도 보정',
  temperature_low: '온도 낮음',
  temperature_high: '온도 높음',
  temperature_critical: '온도 위험',
  humidity_low: '습도 낮음',
  humidity_high: '습도 높음',
  humidity_critical: '습도 위험',
  fahrenheit: '화씨 표시',
  sample_ms: '환경 측정 주기 (ms)',
  log_seconds: '환경 기록 주기 (초)',
  media_seconds: '미디어 기본 표시 시간 (초)',
  media_sort: '미디어 정렬 (0 수동 / 1 이름 / 2 역순)',
  now_layout: 'NOW 레이아웃 (0~3 아트/텍스트, 4 가사, 5 아트+가사)',
  core_order: 'CORE 순서 (0~8 쉼표)',
  core_mask: 'CORE 사용 화면 비트 마스크 (1~511)',
  dday: 'D-Day 날짜',
  night_start: '야간 시작 (자정 이후 분)',
  night_end: '야간 종료 (자정 이후 분)',
  screen_off_minutes: '자동 화면 끄기 (분, 0 사용 안 함)'
};
async function settingsList() {
  const data = await api('/api/settings'), list = $('#setting-list');
  list.replaceChildren();
  const specs = [
    ...data.specs.filter(s => s.name !== 'ap_mode'), {name: 'dday', type: 6, max: 11},
    {name: 'core_order', type: 6, max: 18}
  ];
  for (const spec of specs) {
    const row = element('div', undefined, 'setting'),
          label = element('label', labels[spec.name] || spec.name);
    label.append(element('small', spec.name));
    const input = element(spec.type === 5 ? 'select' : 'input');
    input.id = 'set-' + spec.name;
    label.htmlFor = input.id;
    if (spec.type === 5) {
      for (const [value, text] of [['0', 'OFF'], ['1', 'ON']]) {
        const option = element('option', text);
        option.value = value;
        input.append(option);
      }
    } else if (spec.type === 6) {
      input.type = spec.name === 'dday' ? 'date' : 'text';
      input.maxLength = spec.max - 1;
    } else {
      input.type = 'number';
      input.min = spec.min;
      input.max = spec.max;
      input.step = spec.type === 4 ? '0.01' : '1';
    }
    input.value = data.values[spec.name];
    row.append(label, input, button('저장', async () => {
                 await api('/api/settings', {[spec.name]: input.value});
                 toast((labels[spec.name] || spec.name) + ' 저장됨');
               }));
    row.dataset.search = (labels[spec.name] || '') + ' ' + spec.name;
    list.append(row);
  }
}
$('#settings-filter').oninput = e =>
    all('.setting')
        .forEach(
            row => row.hidden =
                !row.dataset.search.toLowerCase().includes(e.target.value.toLowerCase()));
async function mediaList() {
  const entries = await api('/api/media'), list = $('#media-list');
  list.replaceChildren();
  for (const e of entries) {
    const row = element('div', undefined, 'card'), name = element('input'),
          seconds = element('input'), order = element('input'), enabled = element('input');
    name.value = e.title;
    name.maxLength = 96;
    name.setAttribute('aria-label', '이름');
    seconds.type = order.type = 'number';
    seconds.value = e.seconds;
    seconds.min = 0;
    seconds.max = 3600;
    seconds.setAttribute('aria-label', '표시 시간 (0은 기본값)');
    order.value = e.id;
    order.min = 0;
    order.max = entries.length - 1;
    order.setAttribute('aria-label', '순서');
    enabled.type = 'checkbox';
    enabled.checked = e.enabled;
    enabled.setAttribute('aria-label', '재생 사용');
    row.append(
        element('span', e.path + ' · ' + Math.round(e.bytes / 1024) + ' KiB', 'name'), name,
        seconds, order, enabled, button('저장', async () => {
          await api('/api/media', {
            op: 'edit',
            id: e.id,
            title: name.value,
            seconds: Number(seconds.value),
            order: Number(order.value),
            enabled: enabled.checked
          });
          await mediaList();
        }), button('삭제', async () => {
          if (confirm(e.title + ' 파일을 삭제할까요?')) {
            await api('/api/media', {op: 'delete', id: e.id});
            await mediaList();
          }
        }));
    list.append(row);
  }
}
$('#refresh-media').onclick = run(mediaList);
$('#repair-media').onclick = run(async () => {
  await api('/api/media', {op: 'repair'});
  await mediaList();
});
$('#clear-media').onclick = run(async () => {
  if (confirm('목록에 등록된 모든 미디어를 SD에서 삭제할까요?')) {
    await api('/api/media', {op: 'clear', confirmed: true});
    await mediaList();
  }
});
function progress(value) {
  $('#progress').value = Math.min(1, value);
}
async function operation(fn) {
  if (!store)
    throw new Error('브라우저 저장소를 사용할 수 없어 파일을 준비하거나 전송할 수 없습니다.');
  if (busy) throw new Error('현재 작업을 먼저 마치거나 중단하세요.');
  busy = true;
  controller = new AbortController();
  try {
    await fn(controller.signal);
  } finally {
    busy = false;
    controller = null;
  }
}
function outputPath(file, sync = false) {
  if (sync) return '/media/sync.njv';
  const ext = file.name.split('.').pop().toLowerCase(),
        raw = ['nvi', 'nvv', 'njv', 'mvj', 'msm', 'bmp'].includes(ext),
        video = file.type.startsWith('video/') ||
      ['gif', 'mp4', 'mov', 'webm', 'm4v', 'ogv'].includes(ext);
  return '/media/' +
      file.name.replace(/\.[^.]*$/, '').replace(/[^\p{L}\p{N}_-]/gu, '_').slice(0, 20) + '.' +
      (raw ? ext :
           video ? 'njv' :
                   'nvi');
}
$('#media-file').onchange = run(async e => {
  const file = e.target.files[0];
  if (!file) return;
  URL.revokeObjectURL(previewUrl);
  previewUrl = URL.createObjectURL(file);
  $('#preview').hidden = !file.type.startsWith('video/');
  $('#preview-image').hidden = !file.type.startsWith('image/');
  if (file.type.startsWith('video/'))
    $('#preview').src = previewUrl;
  else if (file.type.startsWith('image/'))
    imageBytes(await loadImage(file), $('#preview-image'));
});
$('#convert').onclick = run(
    () => operation(async signal => {
      const file = $('#media-file').files[0];
      if (!file) throw new Error('파일을 선택하세요.');
      $('#transfer-state').textContent = '브라우저에서 변환 중';
      const meta = await convert(
          file, store, {path: outputPath(file), fps: Number($('#fps').value), signal, progress});
      $('#transfer-state').textContent = `준비됨: ${meta.name} · ${meta.total} bytes`;
    }));
$('#upload').onclick = run(() => operation(async signal => {
                             $('#transfer-state').textContent =
                                 '업로드·재개 중. 마지막 단계에서 전체 파일을 검증합니다.';
                             const meta = await uploadPrepared(store, api, signal, progress);
                             $('#transfer-state').textContent = 'SD 저장·검증 완료: ' + meta.path;
                             await mediaList();
                           }));
$('#cancel-transfer').onclick = () => {
  controller?.abort(new Error('작업을 중단했습니다. 업로드 자료는 재개할 수 있습니다.'));
};
const clock = new SyncClock($('#audio'), action, text => $('#sync-state').textContent = text);
$('#sync-file').onchange = run(async e => {
  await clock.stop();
  URL.revokeObjectURL(syncUrl);
  const file = e.target.files[0];
  if (file) {
    syncUrl = URL.createObjectURL(file);
    $('#audio').src = syncUrl;
  }
});
$('#prepare-sync').onclick =
    run(() => operation(async signal => {
          await clock.stop();
          const file = $('#sync-file').files[0];
          if (!file) throw new Error('원본 영상을 선택하세요.');
          $('#sync-state').textContent = '변환·저장 중';
          await convert(file, store, {path: outputPath(file, true), fps: 20, signal, progress});
          await uploadPrepared(store, api, signal, progress);
          $('#sync-state').textContent =
              'SD 동기 영상 준비 완료. 동기 재생 연결 후 오디오 재생을 누르세요.';
        }));
$('#start-sync').onclick = run(async () => {
  if (busy) throw new Error('영상 준비·전송이 끝난 뒤 동기를 연결하세요.');
  if (!store) throw new Error('브라우저 저장소를 사용할 수 없습니다.');
  if (!$('#sync-file').files[0]) throw new Error('변환에 사용한 원본을 선택하세요.');
  const meta = await store.get();
  if (meta?.path !== '/media/sync.njv' || !await matchesSource(meta, $('#sync-file').files[0]))
    throw new Error('선택한 원본과 준비된 동기 영상이 다릅니다.');
  await clock.start(crypto.getRandomValues(new Uint32Array(1))[0] || 1, meta);
});
$('#stop-sync').onclick = run(() => clock.stop());
for (const name of ['play', 'pause', 'seeked', 'ended'])
  $('#audio').addEventListener(name, () => clock.tick());
document.addEventListener('visibilitychange', () => {
  if (document.hidden && clock.session) {
    $('#audio').pause();
    clock.tick();
  }
});
window.addEventListener('pagehide', () => $('#audio').pause());
function query() {
  return Object.fromEntries(new FormData($('#art-query')));
}
$('#art-query').onsubmit = run(async e => {
  e.preventDefault();
  const response = await api('/api/artwork', {op: 'search', ...query()});
  drawNvi(await response.arrayBuffer(), $('#art-preview'));
});
$('#art-use').onclick = run(async () => {
  await api('/api/artwork', {op: 'use', ...query()});
  toast('사용자 선택 아트로 저장했습니다.');
});
async function blob(kind, file) {
  const body = new FormData();
  body.append('file', file, 'asset');
  return api('/api/blob?' + new URLSearchParams({kind, key: query().key}), body);
}
$('#art-upload').onclick = run(async () => {
  const file = $('#custom-art').files[0];
  if (!file) throw new Error('이미지를 선택하세요.');
  await blob('image', new Blob([imageBytes(await loadImage(file), $('#art-preview'), ART_SIDE)]));
  toast('사용자 이미지 저장됨');
});
$('#lyrics-upload').onclick = run(async () => {
  const file = $('#lyrics-file').files[0];
  if (!file) throw new Error('LRC 파일을 선택하세요.');
  await blob('lyrics', file);
  await action({op: 'artRefresh'});
  toast('가사 저장됨');
});
let artOffset = 0;
async function listArt(reset = false) {
  if (reset) {
    artOffset = 0;
    $('#art-list').replaceChildren();
  }
  const files = await api(
      '/api/files?' +
      new URLSearchParams({dir: '/artwork', offset: artOffset, q: $('#art-filter').value})),
        list = $('#art-list');
  artOffset += files.length;
  $('#more-art').disabled = files.length < 32;
  for (const f of files) {
    const key = f.path.split('/').pop().slice(0, 16), row = element('div', undefined, 'card');
    row.append(
        element(
            'span',
            (f.text || key) +
                (f.missing    ? ' · MISSING' :
                     f.custom ? ' · 사용자 이미지' :
                                '') +
                (f.blocked ? ' · 자동 요청 차단' : ''),
            'name'),
        button(
            '미리보기',
            async () => drawNvi(
                await (await api('/api/file?path=' + encodeURIComponent(f.path))).arrayBuffer(),
                $('#art-preview'))),
        button(f.pinned ? '고정 해제' : '고정', async () => {
          await api('/api/artwork', {op: 'pin', key, enabled: !f.pinned});
          await listArt(true);
        }), button(f.blocked ? '차단 해제' : '차단', async () => {
          await api('/api/artwork', {op: 'block', key, enabled: !f.blocked});
          await listArt(true);
        }), button('다시 받기', async () => {
          await api('/api/artwork', {op: 'refresh', key});
          await action({op: 'artRefresh'});
          await listArt(true);
        }), button('사용자 보호 해제', async () => {
          if (confirm('이 이미지의 사용자 보호를 해제하고 자동 아트 교체를 허용할까요?')) {
            await api('/api/artwork', {op: 'custom', key, enabled: false});
            await listArt(true);
          }
        }), button('이미지 선택', () => {
          $('#art-query [name=key]').value = key;
          $('#custom-art').click();
        }), button('삭제', async () => {
          if (confirm((f.text || key) + ' 아트를 삭제하고 MISSING 상태로 둘까요?')) {
            await api('/api/artwork', {op: 'delete', key});
            await action({op: 'artRefresh'});
            await listArt(true);
          }
        }));
    list.append(row);
  }
}
$('#refresh-art').onclick = run(() => listArt(true));
$('#more-art').onclick = run(() => listArt());
$('#logs').onclick = run(async () => {
  const list = $('#log-list');
  list.replaceChildren();
  let offset = 0;
  for (;;) {
    const files = await api('/api/files?dir=/logs&offset=' + offset);
    for (const f of files) {
      const a = element('a', f.path);
      a.href = '/api/file?path=' + encodeURIComponent(f.path);
      a.download = f.path.split('/').pop();
      list.append(a, element('br'));
    }
    offset += files.length;
    if (files.length < 32) break;
  }
});
$('#diag-export').onclick = () => {
  const url = URL.createObjectURL(new Blob([latest?.diagnostics || ''], {type: 'text/plain'})),
        a = element('a');
  a.href = url;
  a.download = 'nova-diagnostics.txt';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};
for (
    const [id, op, text] of [
        ['diag-clear', 'logsClear', '오류 이력을 지울까요?'],
        ['defaults', 'defaults', '기기 설정을 기본값으로 되돌릴까요?'],
        [
          'factory', 'factory',
          'NOVA NVS 설정과 자격증명을 초기화할까요? SD 파일은 보존되며 남아 있는 SD 프로비저닝 파일은 재부팅 때 다시 적용됩니다.'
        ],
        ['restart', 'restart', '기기를 재시작할까요?']])
  $('#' + id).onclick = run(async () => {
    if (confirm(text)) await action({op, confirmed: true});
  });
$('#firmware-upload').onclick =
    run(() => operation(async signal => {
          const file = $('#firmware').files[0];
          if (!file || file.size > 6291456)
            throw new Error('6 MiB 이하의 서명된 펌웨어를 선택하세요.');
          await store.fromFile(file, '/update/candidate.bin', signal, progress);
          await uploadPrepared(store, api, signal, progress);
          toast('후보 저장됨. 설치 준비 후 기기에서 확인하세요.');
        }));
try {
  const meta = await store?.get();
  if (meta?.ready) $('#transfer-state').textContent = '재개 가능한 자료: ' + meta.name;
} catch (error) {
  toast(error.message);
}
poll();
