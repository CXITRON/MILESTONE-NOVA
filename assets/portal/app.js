import {ART_SIDE, convert, drawNvi, imageBytes, loadImage} from './convert.js';
import {matchesSource, SyncClock, TransferStore, uploadPrepared} from './transfer.js';

const $ = s => document.querySelector(s), all = s => [...document.querySelectorAll(s)];
// Theme: follow the system until the user picks one; the choice is remembered when storage allows.
const root = document.documentElement;
try {
  const saved = localStorage.getItem('nova-theme');
  if (saved === 'light' || saved === 'dark') root.dataset.theme = saved;
} catch (error) {
}
$('#theme').onclick = () => {
  const dark = root.dataset.theme ? root.dataset.theme === 'dark' :
                                    matchMedia('(prefers-color-scheme: dark)').matches;
  root.dataset.theme = dark ? 'light' : 'dark';
  try {
    localStorage.setItem('nova-theme', root.dataset.theme);
  } catch (error) {
  }
};
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
  const deadline = AbortSignal.timeout(data?.op === 'sync' ? 1800 : data?.op === 'commit' ? 900000 : 300000);
  const options = {
    signal: signal ? AbortSignal.any([signal, deadline]) : deadline,
    cache: 'no-store',
    headers: {'X-NOVA': token}
  };
  if (data !== undefined) {
    options.method = 'POST';
    if (data instanceof FormData)
      options.body = data;
    else if (data instanceof Blob) {
      options.headers['Content-Type'] = 'application/octet-stream';
      options.body = data;
    }
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
function button(text, fn, className) {
  const b = element('button', text, className);
  b.onclick = run(fn);
  return b;
}
function field(caption, input, className = 'mini', inline = false) {
  const label = element('label', undefined, className), text = element('span', caption);
  label.append(...(inline ? [input, text] : [text, input]));
  return label;
}
all('nav button').forEach(b => b.onclick = run(async () => {
                            all('.page').forEach(p => p.hidden = p.id !== b.dataset.page);
                            all('nav button').forEach(n => n.classList.toggle('active', n === b));
                            b.scrollIntoView({inline: 'center', block: 'nearest', behavior: 'smooth'});
                            scrollTo({top: 0});
                            if (b.dataset.page === 'media') await mediaList();
                            // children ignores the whitespace text node left in the HTML.
                            if (b.dataset.page === 'settings' &&
                                !$('#setting-list').children.length)
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
for (const [id, enabled] of [['auto-update-on', true], ['auto-update-off', false]])
  $('#' + id).onclick = run(async () => {
    await action({op: 'updateAuto', enabled});
    toast(enabled ? 'AP 종료 후 새 정식 릴리스를 자동 설치합니다.' : '자동 업데이트를 껐습니다.');
  });
async function poll() {
  const polledSession = clock.session;
  try {
    const s = await api('/api/status');
    latest = s;
    token = s.token;
    $('#connection').textContent =
        `${s.network.connected ? 'Wi-Fi 연결됨' : '설정 AP'} · ${s.sd ? 'SD 준비됨' : 'SD 없음'}`;
    $('#connection').dataset.state = s.network.connected ? 'ok' : 'ap';
    $('#device-message').textContent = s.message;
    $('#diagnostics').textContent = s.diagnostics;
    $('#auto-update-state').textContent = s.autoUpdate ?
        '자동 업데이트 켜짐 · 부팅 30초 후와 하루 한 번 확인 · 새 버전은 자동 설치 후 재시작' :
        '자동 업데이트 꺼짐 · 새 버전은 알림만 표시하고 기기에서 길게 OK로 설치';
    $('#wifi-test').textContent = s.network.test;
    const overview = $('#overview');
    overview.replaceChildren();
    for (const [name, value] of [
             ['모드', ['CORE', 'MEDIA', 'NOW'][s.mode]],
             ['온도', s.sensor ? `${s.temperature.toFixed(1)} °C` : '센서 대기'],
             ['습도', s.sensor ? `${s.humidity.toFixed(1)} %` : '센서 대기'],
             ['배터리', s.volts.toFixed(2) + ' V'], ['가동 시간', Math.floor(s.uptime / 60) + '분'],
             ['BLE', s.ble]])
    {
      const stat = element('div', undefined, 'stat');
      stat.append(element('dt', name), element('dd', value));
      overview.append(stat);
    }
    all('[data-mode]').forEach(b => b.classList.toggle('active', Number(b.dataset.mode) === s.mode));
    if (!$('#art-query [name=key]').value && s.track.key)
      for (const key of ['key', 'title', 'artist', 'album'])
        $('#art-query [name=' + key + ']').value = s.track[key];
    const scan = $('#scan-list');
    scan.replaceChildren();
    for (const item of s.network.scan) {
      const b = button('', () => {
        $('#wifi-form [name=ssid]').value = item.ssid;
        $('#wifi-form [name=password]').focus();
      }, item.supported ? 'net' : 'net unsupported');
      b.dataset.level = item.rssi >= -55 ? 4 : item.rssi >= -67 ? 3 : item.rssi >= -78 ? 2 : 1;
      const bars = element('span', undefined, 'bars');
      bars.append(...[1, 2, 3, 4].map(() => element('i')));
      b.append(
          element('span', item.ssid, 'ssid'),
          element('span', `${item.rssi} dBm${item.supported ? '' : ' · 미지원'}`, 'meta'),
          bars);
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
    if (polledSession && clock.session === polledSession &&
        (s.syncStale || s.syncSession !== polledSession)) {
      $('#audio').pause();
      $('#sync-state').textContent = '기기에서 동기가 중지되었습니다. 다시 연결하세요.';
    }
  } catch (error) {
    $('#connection').textContent = '기기 연결 확인 필요';
    $('#connection').dataset.state = 'err';
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
const corePages = ['시계', 'D-Day', '메시지', '대시보드', '날짜 + 메시지', 'D-Day + 시계',
                   '시스템', '집중 타이머', '환경'];
const labels = {
  label: 'D-Day 제목 문구 (D-Day·시계·메시지 화면 상단)',
  dday: 'D-Day 날짜',
  dday_text: 'D-Day를 "12일 남음" 형식으로 표시',
  after_complete: '지난 D-Day를 "완료"로 표시',
  message: '메시지 문구 (부팅·D-Day·메시지·대시보드 화면)',
  hour24: '24시간제',
  seconds: '초 표시',
  timezone: '시간대 (POSIX TZ, 예: KST-9)',
  lcd_brightness: 'LCD 밝기',
  display_inverted: 'LCD 색 반전',
  luminance: '전체 밝기 보정 (%)',
  contrast: '대비 보정',
  screen_off_minutes: '자동 화면 끄기 (분, 0 사용 안 함)',
  burnin: '번인 방지 (1분마다 1픽셀 이동)',
  scroll: '긴 글자 흐르게 표시',
  scroll_speed: '글자 흐름 속도 (px/초)',
  align_left: '글자 왼쪽 정렬',
  time_color: '시간 색',
  date_color: '날짜 색',
  message_color: '메시지 색',
  event_color: 'D-Day 색',
  accent_color: '강조 색',
  muted_color: '보조 글자 색',
  profile: '부팅 시 모드',
  core_start: 'CORE 시작 화면',
  core_mask: 'CORE 사용 화면',
  core_order: 'CORE 화면 순서 (번호 쉼표 구분)',
  cycle: 'CORE 화면 자동 전환',
  cycle_seconds: '자동 전환 간격 (초)',
  focus_seconds: '집중 타이머 기본 시간 (초)',
  now_layout: 'NOW 레이아웃',
  lyrics_view: '가사 표시와 온라인 가사 조회',
  artwork_auto: '앨범아트 자동 조회',
  artwork_cache_mb: '앨범아트 캐시 한도 (MB)',
  artwork_free_mb: 'SD 최소 여유 공간 (MB)',
  media_loop: '미디어 반복 재생',
  media_autoplay: '미디어 자동 재생',
  media_seconds: '미디어 기본 표시 시간 (초)',
  media_sort: '미디어 정렬',
  media_monochrome: '미디어 흑백 표시',
  environment_enabled: '환경 센서 사용',
  environment_mask: '환경 화면 표시 항목',
  fahrenheit: '화씨 표시',
  temperature_offset: '온도 보정 (°C)',
  humidity_offset: '습도 보정 (%)',
  temperature_low: '온도 낮음 기준 (°C)',
  temperature_high: '온도 높음 기준 (°C)',
  temperature_critical: '온도 위험 기준 (°C)',
  humidity_low: '습도 낮음 기준 (%)',
  humidity_high: '습도 높음 기준 (%)',
  humidity_critical: '습도 위험 기준 (%)',
  sample_ms: '환경 측정 주기 (ms)',
  environment_log: '환경 CSV 기록',
  log_seconds: '환경 기록 주기 (초)',
  leds_enabled: 'RGB LED 사용',
  rgb_brightness: 'RGB 밝기',
  night_brightness: '야간 RGB 밝기 상한',
  night_start: '야간 시작',
  night_end: '야간 종료',
  wifi_sleep: 'Wi-Fi 절전',
  boot_sync: '부팅 시 시간 동기화',
  ntp_seconds: '시간 동기화 주기 (초, 0 사용 안 함)',
  retry_seconds: 'Wi-Fi 재시도 최대 간격 (초)',
  lcd_hz: 'LCD SPI 클럭 (Hz)',
  battery_gain: '배터리 전압 배율 보정',
  battery_offset: '배터리 전압 오프셋 (V)',
  thermal_warn: '칩 온도 경고 (°C)',
  thermal_throttle: '칩 온도 감속 (°C)',
  thermal_stop: '칩 온도 정지 (°C)'
};
const groups = [
  ['시계 · D-Day · 메시지',
   ['label', 'dday', 'dday_text', 'after_complete', 'message', 'hour24', 'seconds', 'timezone']],
  ['화면', ['lcd_brightness', 'display_inverted', 'luminance', 'contrast', 'screen_off_minutes',
          'burnin', 'scroll', 'scroll_speed', 'align_left']],
  ['색상', ['time_color', 'date_color', 'message_color', 'event_color', 'accent_color',
          'muted_color']],
  ['모드 · CORE 화면', ['profile', 'core_start', 'core_mask', 'core_order', 'cycle', 'cycle_seconds',
                     'focus_seconds']],
  ['NOW · 가사 · 앨범아트', ['now_layout', 'lyrics_view', 'artwork_auto', 'artwork_cache_mb',
                         'artwork_free_mb']],
  ['MEDIA', ['media_loop', 'media_autoplay', 'media_seconds', 'media_sort', 'media_monochrome']],
  ['환경 센서', ['environment_enabled', 'environment_mask', 'fahrenheit', 'temperature_offset',
             'humidity_offset', 'temperature_low', 'temperature_high', 'temperature_critical',
             'humidity_low', 'humidity_high', 'humidity_critical', 'sample_ms',
             'environment_log', 'log_seconds']],
  ['RGB LED · 야간', ['leds_enabled', 'rgb_brightness', 'night_brightness', 'night_start',
                    'night_end']],
  ['네트워크 · 시간 동기화', ['wifi_sleep', 'boot_sync', 'ntp_seconds', 'retry_seconds']],
  ['고급', ['lcd_hz', 'battery_gain', 'battery_offset', 'thermal_warn', 'thermal_throttle',
          'thermal_stop']]
];
const choices = {
  profile: ['CORE', 'MEDIA', 'NOW'],
  core_start: corePages,
  now_layout: ['큰 아트 + 제목/아티스트', '작은 아트 + 상세', '텍스트', '큰 아트 + 제목/앨범',
               '가사', '아트 + 가사'],
  media_sort: ['수동', '이름순', '이름 역순']
};
const bits = {core_mask: corePages, environment_mask: ['온도', '습도']};
const sliders = new Set(
    ['lcd_brightness', 'rgb_brightness', 'night_brightness', 'luminance', 'contrast', 'scroll_speed']);
const hex = n => n.toString(16).padStart(2, '0');
const toHex = v => '#' + hex(Math.round((v >> 11) * 255 / 31)) +
    hex(Math.round(((v >> 5) & 63) * 255 / 63)) + hex(Math.round((v & 31) * 255 / 31));
const fromHex = h => {
  const n = parseInt(h.slice(1), 16);
  return ((n >> 19) & 31) << 11 | ((n >> 10) & 63) << 5 | ((n >> 3) & 31);
};
// Builds the input for one setting and returns [element, read()] where read() gives the API value.
// Returns [node, read, field]: `field` is the input that the row label points at.
function control(spec, value) {
  const name = spec.name;
  if (spec.type === 5 && !choices[name]) {
    const input = element('input');
    input.type = 'checkbox';
    input.className = 'switch';
    input.setAttribute('role', 'switch');
    input.checked = String(value) === '1';
    return [input, () => input.checked ? '1' : '0'];
  }
  if (choices[name]) {
    const input = element('select');
    choices[name].forEach((text, index) => {
      const option = element('option', `${index} · ${text}`);
      option.value = String(index);
      input.append(option);
    });
    input.value = String(value);
    return [input, () => input.value];
  }
  if (sliders.has(name) && Number.isFinite(spec.min) && Number.isFinite(spec.max)) {
    const wrap = element('div', undefined, 'slider'), range = element('input'),
          out = element('output');
    range.type = 'range';
    range.min = spec.min;
    range.max = spec.max;
    range.step = 1;
    range.value = value;
    out.textContent = range.value;
    range.addEventListener('input', () => out.textContent = range.value);
    wrap.append(range, out);
    return [wrap, () => range.value, range];
  }
  if (bits[name]) {
    const box = element('div', undefined, 'bits'), checks = [];
    bits[name].forEach((text, index) => {
      const item = element('label'), check = element('input');
      check.type = 'checkbox';
      check.checked = (Number(value) >> index) & 1;
      item.append(check, ' ' + text);
      box.append(item);
      checks.push(check);
    });
    return [box, () => String(checks.reduce((n, c, i) => n | (c.checked ? 1 << i : 0), 0))];
  }
  const input = element('input');
  if (name.endsWith('_color')) {
    input.type = 'color';
    input.value = toHex(Number(value));
    return [input, () => String(fromHex(input.value))];
  }
  if (name === 'night_start' || name === 'night_end') {
    input.type = 'time';
    input.value = String(Math.floor(value / 60)).padStart(2, '0') + ':' +
        String(value % 60).padStart(2, '0');
    return [input, () => {
      const [h, m] = input.value.split(':').map(Number);
      return String(h * 60 + m);
    }];
  }
  if (spec.type === 6) {
    input.type = name === 'dday' ? 'date' : 'text';
    input.maxLength = spec.max - 1;
  } else {
    input.type = 'number';
    input.min = spec.min;
    input.max = spec.max;
    input.step = spec.type === 4 ? '0.01' : '1';
  }
  input.value = value;
  return [input, () => input.value];
}
async function settingsList() {
  const data = await api('/api/settings'), list = $('#setting-list');
  list.replaceChildren();
  const specs = new Map([
    ...data.specs.filter(s => s.name !== 'ap_mode'), {name: 'dday', type: 6, max: 11},
    {name: 'core_order', type: 6, max: 18}
  ].map(s => [s.name, s]));
  const grouped = new Set(groups.flatMap(([, names]) => names));
  const rest = [...specs.keys()].filter(n => !grouped.has(n));
  [...groups, ...(rest.length ? [['기타', rest]] : [])].forEach(([title, names], index) => {
    const group = element('details', undefined, 'setting-group');
    group.open = index === 0;
    const summary = element('summary', title);
    group.append(summary);
    for (const name of names) {
      const spec = specs.get(name);
      if (!spec) continue;
      const row = element('div', undefined, 'setting'),
            label = element('label', labels[name] || name);
      label.append(element('small', name));
      const [input, read, target = input] = control(spec, data.values[name]);
      target.id = 'set-' + name;
      label.htmlFor = target.id;
      row.addEventListener('input', () => row.classList.add('dirty'));
      row.addEventListener('change', () => row.classList.add('dirty'));
      row.append(label, input, button('저장', async () => {
                   await api('/api/settings', {[name]: read()});
                   row.classList.remove('dirty');
                   toast((labels[name] || name) + ' 저장됨');
                 }));
      row.dataset.search = (title + ' ' + (labels[name] || '') + ' ' + name).toLowerCase();
      group.append(row);
    }
    const count = group.querySelectorAll('.setting').length;
    if (count) {
      summary.append(element('span', String(count), 'count'));
      list.append(group);
    }
  });
}
$('#settings-filter').oninput = e => {
  const query = e.target.value.trim().toLowerCase();
  all('.setting-group').forEach(group => {
    let visible = 0;
    group.querySelectorAll('.setting').forEach(row => {
      row.hidden = query && !row.dataset.search.includes(query);
      visible += !row.hidden;
    });
    group.hidden = !visible;
    if (query) group.open = true;
  });
};
async function mediaList() {
  const entries = await api('/api/media'), list = $('#media-list');
  list.replaceChildren();
  for (const e of entries) {
    const row = element('div', undefined, 'card media'), name = element('input'),
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
    const buttons = element('div', undefined, 'btns');
    buttons.append(button('저장', async () => {
      await api('/api/media', {
        op: 'edit',
        id: e.id,
        title: name.value,
        seconds: Number(seconds.value),
        order: Number(order.value),
        enabled: enabled.checked
      });
      await mediaList();
    }, 'sm primary'), button('삭제', async () => {
      if (confirm(e.title + ' 파일을 삭제할까요?')) {
        await api('/api/media', {op: 'delete', id: e.id});
        await mediaList();
      }
    }, 'sm danger'));
    const info = element('span', e.title, 'name');
    info.append(element('small', e.path + ' · ' + Math.round(e.bytes / 1024) + ' KiB'));
    row.append(
        info, field('이름', name, 'mini wide'), field('표시 시간 (초, 0은 기본)', seconds),
        field('순서', order), field('재생 사용', enabled, 'mini check', true), buttons);
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
// Sync preparation has two phases (browser conversion, then SD upload); show both on the sync page.
function syncProgress(phase) {
  return value => {
    const v = Math.min(1, value);
    $('#sync-progress').value = v;
    $('#sync-state').textContent = `${phase} ${Math.round(v * 100)}%`;
    progress(v);
  };
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
                             const meta = await uploadPrepared(store, api, signal, progress, percent => {
                               progress(percent / 100);
                               $('#transfer-state').textContent =
                                   `기기에서 파일 검증 중 ${Math.round(percent)}%`;
                             }, (n, why) => $('#transfer-state').textContent =
                                 `전송 재시도 ${n}/3: ${why}`);
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
          $('#sync-progress').value = 0;
          $('#sync-state').textContent = '변환 중 0%';
          // The picker may be missing when the page is an older cached copy; default to 20.
          const syncFps = Number($('#sync-fps')?.value) || 20;
          const syncQuality = Number($('#sync-quality')?.value) || .65;
          await convert(file, store,
                        {path: outputPath(file, true), fps: syncFps, quality: syncQuality, signal,
                         progress: syncProgress('1/2 브라우저 변환')});
          $('#sync-progress').value = 0;
          // The device validates every frame before replying; it cannot answer polls meanwhile.
          const verifyStart = percent => {
            $('#sync-progress').value = percent / 100;
            $('#sync-state').textContent =
                `3/3 기기에서 영상 검증 중 ${Math.round(percent)}% (긴 영상은 몇 분 걸립니다)`;
          };
          const up = syncProgress('2/2 SD 업로드'), t0 = Date.now(), total = (await store.get()).total;
          await uploadPrepared(store, api, signal, v => {
            up(v);
            const kb = Math.round(v * total / 1024 / Math.max(1, (Date.now() - t0) / 1000));
            $('#sync-state').textContent += ` · ${kb} KB/s`;
          }, verifyStart, (n, why) => $('#sync-state').textContent = `전송 재시도 ${n}/3: ${why}`);
          $('#sync-progress').value = 1;
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
  $('#start-sync').disabled = true;
  $('#audio').pause();
  $('#audio').controls = false;
  try {
    await clock.start(crypto.getRandomValues(new Uint32Array(1))[0] || 1, meta);
    $('#sync-state').textContent = '연결 완료 · 아래 오디오 재생 버튼을 누르세요. AP 연결을 유지하세요.';
  } finally {
    $('#start-sync').disabled = false;
    $('#audio').controls = true;
  }
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
    const key = f.path.split('/').pop().slice(0, 16), row = element('div', undefined, 'card art');
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
      list.append(a);
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
    const [id, op, text, done] of [
        ['diag-clear', 'logsClear', '오류 이력을 지울까요?', '오류 이력을 지웠습니다.'],
        ['defaults', 'defaults', '기기 설정을 기본값으로 되돌릴까요?', '설정을 기본값으로 되돌렸습니다.'],
        [
          'factory', 'factory',
          'NOVA NVS 설정과 자격증명을 초기화할까요? SD 파일은 보존되며 남아 있는 SD 프로비저닝 파일은 재부팅 때 다시 적용됩니다.',
          '설정과 자격증명을 초기화했습니다.'
        ],
        ['restart', 'restart', '기기를 재시작할까요?', '재시작 요청을 보냈습니다. 잠시 후 다시 연결하세요.']])
  $('#' + id).onclick = run(async () => {
    if (!confirm(text)) return;
    await action({op, confirmed: true});
    toast(done);
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
// Empty art preview: a hint instead of a blank box (re-drawn by drawNvi once a search runs).
{
  const canvas = $('#art-preview'), ctx = canvas.getContext('2d');
  ctx.fillStyle = getComputedStyle(root).getPropertyValue('--muted') || '#888';
  ctx.font = '600 15px system-ui, sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('검색하면 여기에 표시됩니다', canvas.width / 2, canvas.height / 2);
}
poll();
