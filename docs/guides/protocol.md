# NOW 공급 경로와 파일 계약

## 데이터 출처

| 정보 | iOS AMS | BLE helper | 로컬 SD |
|---|---|---|---|
| title / artist / album | Track attributes 2/0/1 | snapshot 문자열 | key 계산에 사용 |
| play / pause / rate / position | Player PlaybackInfo | state frame (rate 1.0) | 공급하지 않음 |
| duration | Track Duration | state frame | video 자체 duration만 |
| artwork | AMS에서 제공하지 않음 | metadata로 key 결정 | `/artwork/<key>.nvi`(200×200), miss 시 Worker v3 JPEG 200(갱신 전 MAC1) |
| lyrics | AMS에서 제공하지 않음 | metadata/position 제공 | `/lyrics/<key>.lrc`, 없으면 Worker `/v1/lyrics` |

ESP32-S3에서 Bluetooth Classic을 사용하지 않는다. firmware가 metadata를 만들거나
네트워크 성공을 가정하지 않는다. AMS 속성은 독립 알림이므로 400 ms quiet burst를
하나로 합친다. track change 시 오래된 position은 버리고 최근 authoritative 위치만
수용한다. AMS에는 모든 player에 공통으로 보장되는 영속 track ID가 없다.

## Helper GATT v1

Service `33b2a140-9d4a-4e1b-b952-a1d398c30001`

- Input `...0002`: encrypted write **with response**. 기본 MTU 23에서도 동작.
- Control `...0003`: notify/read characteristic은 기존 helper 도구와의 프로토콜 호환을 위해
  유지하지만 현재 helper snapshot만으로 원격 제어 가능 상태를 선언하지 않는다.
  온디바이스 재생 명령은 실제 AMS Remote Command 지원 목록이 있는 경우에만 실행한다.
- BLE pairing은 bonding/LE secure connections의 Just Works다. metadata용이며
  credentials나 OTA 제어를 이 characteristic으로 받지 않는다.

모든 input packet: version(1 byte, 값 1), type(1 byte), transaction(LE uint32), payload.
하나의 transaction은 5초 내 완료한다. 20-byte ATT payload에 맞춰 작은 frame을
사용할 수 있다. 문자열 continuation은 UTF-8 codepoint 경계에서 자른다.

| Type | Payload | 의미 |
|---:|---|---|
| 1 BEGIN | empty 또는 16자리 소문자 hex key | 새 staging snapshot 시작 |
| 2 TITLE | UTF-8 0..240 bytes (여러 packet을 이어 붙임) | 누적 최대 240 bytes |
| 3 ARTIST | 위와 같음 | 빈 값도 frame을 보내야 함 |
| 4 ALBUM | 위와 같음 | 빈 값도 frame을 보내야 함 |
| 5 STATE | position_ms:uint32, duration_ms:uint32, playing:uint8(0/1) | duration 0은 unknown |
| 6 COMMIT | empty | 모든 field가 있어야 원자적으로 반영 |

순서 오류/잘못된 크기/다른 transaction/시간 초과는 staging snapshot을 폐기한다.
queue overflow는 세션을 끊고 재동기화하여 서로 다른 곡의 부분 field를 섞지 않는다.
BEGIN에 명시적 key가 없으면 firmware가 artist/title/album/duration을 hash한다.
명시적 key와 긴 field를 작은 MTU로 보내려면 key 없는 BEGIN 및 계산된 key 방식을 쓴다.

`scripts/media/ble_helper.py --pack`는 stdin JSON을 hex packet으로 출력한다.
`--address <BLE address>`는 `bleak` 설치 후 실제 기기에 보낸다. 한 줄 JSON 예:

```json
{"title":"내 테스트 곡","artist":"내 아티스트","album":"내 앨범","duration_ms":60000,"position_ms":10000,"playing":true}
```

이 예는 프로토콜 검사 데이터이며 실제 플레이어를 읽은 값이 아니다. 통합 앱은
자신이 접근 권한을 가진 player API에서 snapshot을 제공해야 한다. 현재 helper는 metadata/position 공급 경로다. PC helper의 notify 수신 코드는
player를 실행하는 구현이 아니며, iPhone AMS 제어와 구별한다.

## 곡 key / cache

artist/title/album을 각각 `UTF-8 byte length:LE uint32 + 원래 UTF-8 bytes`로 이어
붙이고 마지막에 duration_ms LE uint32를 넣는다. 이를 FNV-1a 64-bit로 hash하여
16자리 lowercase hex로 표현한다. Unicode normalization/case folding은 하지 않는다.
입력 문자열은 수신된 최대 240-byte 형태를 기준으로 한다. hash 충돌의 수학적 가능성은
남지만 안전한 파일 경로와 일관된 도구 계산을 제공한다.

LRC는 파일 전체 32 KiB, 최대 512개 timestamp entry, 한 줄 1024 bytes 제한이다.
한 줄 복수 timestamp는 최대 16개이며 한도를 넘으면 truncated flag를 기록한다.
상한 초과 파일은 SD worker가 거절한다. 매 프레임 파일을 다시 열거나 전체 cache를
순회하지 않는다. 온라인 provider는 local hit를 우선하고 실패 시 현재 MediaSession을 유지한다.
endpoint는 `src/network/Endpoints.h` 한 곳에 둔다.

## 온라인 가사 Worker

같은 MILESTONE Worker의 `POST /v1/lyrics`는
`Content-Type: application/x-www-form-urlencoded`로 `title`, `artist`(필수),
`album`(선택), `duration`(선택, 초 단위 1~3600)을 받는다.
인코딩된 요청 본문은 최대 1400 bytes, 정규화한 각 문자열은 최대 192 codepoints다.
빈 필수 필드, 중복 필드, 제어 문자는 거절한다. 알 수 없는 재생 길이는 생략한다.
펌웨어는 ms를 초로 반올림하며 1시간을 초과하는 곡의 온라인 조회는 하지 않는다.

```sh
curl --fail-with-body -X POST \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'title=I Want to Live' \
  --data-urlencode 'artist=Borislav Slavov' \
  --data-urlencode "album=Baldur's Gate 3 (Original Game Soundtrack)" \
  --data 'duration=233' \
  https://milestone-artwork.typhoon-individual.workers.dev/v1/lyrics \
  -o lyrics.lrc
```

| 상태 | 의미 |
|---|---|
| 200 | `text/plain; charset=utf-8`, `X-Milestone-Lyrics: 1`, 검증된 동기화 LRC |
| 204 | 일치하는 동기화 가사 없음. 무가사 곡/일반 텍스트만 있는 곡도 포함 |
| 400 / 413 / 415 | 입력 오류 / 본문 상한 초과 / 다른 Content-Type |
| 502 / 504 | 제공자 오류·잘못된 응답 / 조회 제한 시간 초과 |
| 503 | 제공자 요청 제한 또는 대기열 포화. `Retry-After`초 이후 다시 시도 |

Worker는 [LRCLIB 공식 API](https://lrclib.net/docs)의 `/api/get`에 명시적인 User-Agent를
붙인다. 제목·아티스트가 정규화 후 같아야 하고, 알려진 길이는 응답과 ±2초 이내여야 한다.
추측 검색 결과나 `plainLyrics`를 동기화 가사로 바꾸지 않는다. JSON은 최대 256 KiB,
최종 LRC는 위 기기 parser 한도를 따른다. 제공자 조회는 대기 시간을 포함해 6초로 제한한다.
isolate 안에서는 요청을 직렬 처리하고 250ms 간격을 두며 진행·대기 합계 최대 4개만 받는다.
429의 Retry-After는 isolate와 edge cache에 반영한다. 전 세계 모든 edge를 하나의
전역 요청 제한 장치로 묶는 구조는 아니다.

양성 결과는 30일, 204는 1시간 edge cache에 보관한다. 정규화한 제목·아티스트·앨범과
재생 길이가 cache key에 포함된다. 502/504는 가사 없음으로 캐시하지 않는다.
기기는 응답 수신 후 UTF-8·LRC 구조·한도를 다시 검증하고, 단일 SD worker에서
기존 `/lyrics/<key>.lrc`가 없는지 재검사한 뒤 원자 저장한다. 기존 사용자 파일이
생겼으면 온라인 결과의 저장과 화면 적용을 모두 포기한다. 일반 SD 쓰기 실패는 RAM
표시를 허용한다. 네트워크·파싱은 service worker에서 실행하며 화면에는 맞는 generation만 전달한다.
기기 HTTP timeout은 이 경로만 8초이고 읽기 반복 사이에서 전체 경과 15초를 검사한다.
따라서 제한 시간이 지난 시점의 blocking read까지 즉시 중단하는 hard deadline은 아니다.

로컬 파일이 없고 `lyrics_view`가 켜진 곡에서 한 번 자동 조회한다. SD revision으로 같은 곡을
다시 읽는 것만으로 자동 시도 기록을 초기화하지 않는다. 실패를 반복
조회하지 않으며 곡 재선택/명시적 자산 재읽기에서 다시 시도한다. SD 부재에서도 RAM 표시가 가능하다.
API 구현과 배포 상태는 구분한다. 이 경로는 Worker의 새 코드를 배포한 뒤 외부에서 사용할 수 있다.

## 미디어 형식

CRC32는 reflected 0xEDB88320(init/final XOR 0xffffffff), 아래 미디어 정수와
RGB565는 little endian이다. 전체 크기는 2 GiB 미만이다.

| 형식 | header 및 payload | 검증/상한 |
|---|---|---|
| NVI1 | magic4 + width:u16=S + height:u16=S + payload:u32=S×S×2 + CRC:u32, RGB565 | S ∈ {160, 200, 240}, 정확히 16+S×S×2 bytes |
| NVV1 | magic4 + width/height:u16=S + fps:u16 + reserved:u16=0 + frames:u32 | 매 frame CRC:u32 + RGB565 S×S×2 bytes |
| NJV1 | NVV1과 같은 16-byte header, S×S baseline JPEG | 각 frame length:u32 + CRC:u32 + JPEG, JPEG 4..49152 bytes |
| MVJ1 | NJV1과 같은 구조, 128×128 JPEG | 기존 SD 영상 호환, 표시 때 240px로 확대 |

S는 정사각형 한 변이다. 현재 생성 규격은 MEDIA **240**, NOW 아트 캐시 **200**이며,
160은 이전 NOVA 파일 호환용으로 읽기만 한다.
| MSM1 | magic4 + version:u8=1 + flags:u8 + width/height:u8=128 + frames:u16 + color:u16 + duration:u32 + payload:u32 + CRC:u32 | 최대 4 MiB/4096 frames, color 0 mono1bit/1 RGB332 |
| BMP | Windows DIB header ≥40 bytes, 24bpp uncompressed | 가로/세로 1..320, 양/음 height와 4-byte row alignment |

NVV1/NJV1/MVJ1은 FPS 1..30, 최대 648000 frames 및 6시간이다. MSM1은
flags bit0 animated, bit1 loop이며 frame은 type:u8 + duration_ms:u16 + size:u16 + payload.
type0은 전체 raw frame, type1은 이전 frame에 XOR하는 RLE다. 첫 frame은 raw여야 한다.
MSM1 header의 payload CRC와 누적 duration도 일치해야 한다. 1-frame duration은 0이다.

NIX1 index는 magic4 + 원본 파일 크기:u32 + frame 수:u32 + reserved:u32 후
각 frame의 offset:u32와 시작 시각:u32를 저장한다. worker가 전체 검증 후 만들며
호환되지 않는 index는 재생 시 재검증·재생성한다. JPEG는 읽는 frame마다 CRC를 검사한다.
NMC1 catalog의 CRC A/B records에 최대 64개 항목의 경로/제목/사용/순서/표시 시간을 저장한다.
표시 시간 0은 전역 media_seconds를 따른다. 사용자가 index/catalog를 편집할 필요는 없다.

## Artwork Worker / 캐시

### 다른 프로젝트에서 호출하기

Base URL: `https://milestone-artwork.typhoon-individual.workers.dev`.
현재 NOVA의 주소 기준은 `src/network/Endpoints.h`다. 아래 서버 규격은 기존
`MILESTONE_Legacy/MILESTONE_Core/services/artwork-worker/src/worker.js`의 라우트와
실제 배포 응답을 확인해 기록했다. 서버를 수정하거나 재배포하지 않았다.

세 버전 모두 **POST + application/x-www-form-urlencoded**이며 JSON 요청은 지원하지 않는다.
API key/Authorization은 현재 필요하지 않다. 이 API의 아트는 곡 metadata로 검색한
앨범 표지이며 iPhone이 보내는 이미지나 아티스트 프로필 사진이 아니다.

| form 필드 | 필수 | 내용 |
|---|---|---|
| title | 예 | 곡 제목 |
| artist | 예 | 아티스트 이름 |
| album | 아니요 | 앨범 이름. 매칭과 캐시 키에 사용 |

UTF-8로 URL encode한다. **인코딩된 body 전체는 1400 bytes 이하**여야 한다.
서버는 NFKC 정규화·공백 정리 후 각 필드를 JavaScript 문자열 192 code units까지 자른다.
한글은 percent-encoding 뒤 길이가 늘어나므로 원래 문자열 길이만 검사해서는 안 된다.

| 경로 | HTTP 200 body / Content-Type | 용도 |
|---|---|---|
| /v1/artwork | JPEG bytes / image/jpeg | 일반 앱·웹 백엔드에서 이미지 파일로 사용 |
| /v2/artwork | MAB1, 1464 bytes / application/vnd.milestone.artwork-bitmap | 60×60 + 88×88 흑백 1-bit, 행 단위 LSB-first |
| /v3/artwork | baseline JPEG 200×200 / image/jpeg | 현재 NOVA. 2026-10-03 갱신 전에는 MAC1 22704 bytes |

2026-10-03 MILESTONE-Core `services/artwork-worker`의 v3를 NOVA용으로 교체했다.
응답은 SOF0 baseline·200×200·3 components·64 KiB 이하 JPEG다. 카탈로그 CDN에 200×200을
직접 요청해 baseline이면 그대로 전달하고, progressive나 다른 크기면 Wasm MozJPEG로
다시 인코딩한다(`X-Milestone-Transcoded`). **MAC1을 기대하는 옛 MILESTONE 펌웨어는
이 Worker 배포 후 v3 아트를 표시하지 못한다.** 아래 MAC1 설명은 갱신 전 형식이며,
NOVA는 배포 전 응답 호환을 위해 MAC1도 계속 받는다.

응답은 이미지 바이너리 자체다. JSON이나 이미지 URL을 반환하지 않는다.
v1은 검색 공급자의 thumbnail을 전달하므로 해상도가 고정되어 있지 않다.

```sh
curl --silent --show-error --max-time 30 \
  --write-out 'HTTP %{http_code}\n' \
  'https://milestone-artwork.typhoon-individual.workers.dev/v1/artwork' \
  --data-urlencode 'title=Blinding Lights' \
  --data-urlencode 'artist=The Weeknd' \
  --data-urlencode 'album=After Hours' \
  --output cover.jpg
```

Node.js 18+의 서버 측 예시:

```js
import { writeFile } from 'node:fs/promises';

const body = new URLSearchParams({
  title: 'Blinding Lights', artist: 'The Weeknd', album: 'After Hours'
});
if (Buffer.byteLength(body.toString()) > 1400) throw new Error('Metadata too long');
const response = await fetch(
  'https://milestone-artwork.typhoon-individual.workers.dev/v1/artwork',
  { method: 'POST', body, signal: AbortSignal.timeout(30000) }
);
if (response.status === 204) throw new Error('Artwork not found');
if (response.status !== 200) throw new Error(`Artwork HTTP ${response.status}`);
if (response.headers.get('content-type')?.split(';')[0] !== 'image/jpeg')
  throw new Error('Unexpected artwork format');
await writeFile('cover.jpg', Buffer.from(await response.arrayBuffer()));
```

`URLSearchParams`가 Content-Type을 지정한다. 브라우저의 다른 origin에서 직접 읽을 수
있도록 CORS를 열어 둔 서버는 아니다. 웹 프로젝트는 자체 백엔드에서 호출해 이미지로
전달하거나 저장해야 한다. `<img src=".../v1/artwork">`도 GET이므로 동작하지 않는다.

| status | 의미 / 처리 |
|---|---|
| 200 | 이미지 있음. Content-Type과 body를 확인 |
| 204 | 조회에서 이미지 확보 못함. 빈 body이며 JPEG decode하지 않음 |
| 400 | 필수 title/artist 누락 또는 공백 |
| 413 | 선언된 body 길이가 1400 bytes 초과 |
| 404 | 잘못된 경로/HTTP method |
| 502 | 서버의 조회·읽기·변환 처리 실패 |

`response.ok`는 204도 true이므로 200 여부를 별도로 확인한다. body 제한 위반 중
스트리밍 읽기에서 발견된 경우 현재 서버는 502를 반환할 수도 있다. 정상 결과의
Cache-Control은 30일, 조회 실패 204는 6시간이며 외부 공급자 일시 실패도 204가 될 수 있다.
서버는 정규화한 title/artist/album과 format별로 캐시한다. `X-Milestone-Upstream`은
공급자 조회 경로를 확인하는 진단 header다. 현재 요청 handler에는 별도 API key나
애플리케이션별 rate limit을 구현하지 않았다.

2026-09-25 위 v1 예제로 실서버 HTTP 200 / image/jpeg / 3536 bytes / 96×96 JPEG를
확인했다. v3도 실서버 22704-byte MAC1의 치수·CRC를 확인했다. 이는 해당 곡의 응답
검증이며 모든 곡의 검색 성공이나 고해상도 원본 제공을 뜻하지 않는다.

### MAC1 해석과 NOVA 캐시

MAC1 응답은 다음과 같으며 multi-byte 정수는 big-endian이다.

| offset | 길이 | 값 |
|---:|---:|---|
| 0 | 4 | ASCII MAC1 |
| 4 | 4 | small width/height, large width/height: 60,60,88,88 |
| 8 | 2 | small payload 길이 7200 |
| 10 | 2 | large payload 길이 15488 |
| 12 | 4 | offset 16부터 끝까지 CRC-32/ISO-HDLC (zlib CRC32와 동일) |
| 16 | 7200 | 60×60, row-major RGB565 |
| 7216 | 15488 | 88×88, row-major RGB565 |

픽셀은 `word = (byte0 << 8) | byte1`, red=`(word >> 11) & 31`,
green=`(word >> 5) & 63`, blue=`word & 31`이다. NOVA는 길이와 CRC가 맞아야
88px 부분을 200px로 부드럽게(bilinear) 확대한다. HTTP 200 외 status/불완전 body/다른 format은
성공으로 취급하지 않는다.

JPEG 응답은 ESP32 JPEG decoder로 크기(200×200)를 다시 확인한 뒤 NVI1 200×200으로 캐시한다.
디코딩 실패, 크기 불일치, 64 KiB 초과는 조회 실패로 처리한다.

SD 아트는 `<key>.nvi`이며 `.nvi.pin`/`.nvi.custom` 보호 표시,
`<key>.block`/`<key>.missing` 자동 조회 억제 표시, `<key>.meta` 곡 정보+CRC,
`.nvi.used` 최근 사용 표시를 함께 관리한다. 파일 저장은 `.tmp` 검증 → `.bak` → 교체다.
캐시가 설정 한도를 넘을 때만 보호되지 않은 가장 오래된 이미지를 지운다.
SD 여유 공간 하한은 새 저장을 막는 조건이며 강제 정리 조건으로 쓰지 않는다.
아트/LRC/보호 상태 변경과 SD 재탑재는 asset revision을 갱신한다. App은 새 세대의
자료를 다시 읽고 오래된 SD/HTTP 결과는 화면에 적용하지 않는다. 자동 요청 직전과
저장 직전에 pin/custom/block/MISSING을 검사한다. 명시적 재조회가 실패하면 기존의
정상 아트를 유지하며, 새 응답 검증·원자 저장이 성공한 뒤에만 교체한다.

## 설정 AP HTTP API

AP 인터페이스로 들어온 요청만 처리한다. mutation은 현재 AP session의 128-bit token을
`X-NOVA` header로 보내야 하며 Host와 Content-Type도 검사한다. CORS를 열지 않는다.
JSON body는 8 KiB, multipart chunk는 256 KiB로 제한하고 연결 종료/초과 body를 거절한다.
암호/공개키 원문은 status/settings/file API로 반환하지 않는다.

| 경로 | 동작 |
|---|---|
| GET /api/status | session token, mode, 센서/연결/진단, sync 상태 |
| GET/POST /api/settings | SettingSpec 범위/현재 값, 문자열 값으로 설정 적용 |
| POST /api/action | mode, scan, wifi/wifiUse/wifiDelete, ap/apClose, time/ntp, sensor, defaults/factory, legacyImport, update*, ota, sync*, logsClear/restart |
| GET/POST /api/media | 목록, edit/delete/clear/repair |
| GET /api/files | dir=/artwork 또는 /lyrics 또는 /logs, offset과 q; 최대 32개 |
| GET /api/file | 공개 media/artwork/lyrics/logs 파일을 16 KiB 단위로 전달 |
| POST /api/transfer | begin/status/commit/abort. begin의 id/path/total이 일치하면 durable offset 반환 |
| POST /api/blob | query id/offset/crc + multipart chunk, 또는 kind=image/lyrics/key |
| POST /api/artwork | search/use, pin/custom/block, refresh/delete |

begin은 새 전송 ID(uint32, 0 제외), 대상 경로, 크기, 교체 여부를 받는다.
chunk의 크기·CRC·offset을 확인한 뒤 `.part`에 쓰고 CRC checkpoint를 원자적으로 저장한다.
commit은 전체 format/프레임/CRC 검증 후 교체한다. 손상/중단 후보는 기존 파일을 대체하지 않는다.
인터넷 signed 후보는 `/update/candidate.bin`만 사용하고 미디어 검증 대신 Firmware 검증을 거친다.

## AP Sync 시간 계약

1. `/media/sync.njv`를 변환·업로드 완료한다.
2. action `syncStart`에 새 `session` ID와 변환 결과의 `size`(bytes), `crc`(전체
   파일 CRC32 unsigned)를 전달한다. SD의 NJV1 전체 프레임 검증 중 같은 CRC를
   계산해 크기와 일치할 때만 길이를 확정한다. 준비한 파일과 다른 정상 영상도 거절한다.
3. `sync`: session, 증가하는 sequence, position(ms), playing. 브라우저 오디오가 시간 기준이다.
4. device는 같은 session과 새로운 sequence만 수용한다. 2.5초 동안 제어가 없으면
   그 deadline 위치에서 멈춘다. pause 중에는 위치를 진행시키지 않는다.
5. `syncStop`은 종료할 `session`을 포함한다. 다른 활성 세션의 종료 요청은 거절한다.
   브라우저는 start/stop을 직렬화하고 진행 중인 tick이 끝나기를 기다린다.
   동기 종료는 SD 파일을 유지해 재연결을 허용한다. BACK/AP 종료·모드 변경은
   완료된 Sync 파일도 정리한다. 열 중지 상태에서는 새 재생/동기 시작을 거절한다.

이전 버전 브라우저의 준비 데이터에 전체 CRC가 없으면 다시 변환해야 한다.
CRC는 파일 동일성/전송 오류 검사용이며 서명이나 암호학적 인증을 대신하지 않는다.

브라우저는 대략 250 ms마다 전송하고 왕복 지연의 절반을 상한 내 보정한다.
샘플 시각/네트워크/패널 지연이 있으므로 실물 동기 오차를 따로 측정해야 한다.

## NOVA 서명 업데이트

manifest JSON: target=`milestone-nova-s3`, version, raw GitHub NOVA 저장소의 HTTPS url,
size(1024..6291456), signed 파일 전체의 소문자 sha256(64자리).
후보는 ESP32-S3 chip ID와 `MILESTONE-NOVA` application descriptor를 검사한다.
Arduino-ESP32 3.3.11의 RSA-PSS/SHA256 형식: unsigned ESP image + 512-byte footer.
RSA 2048..4096-bit signature 뒤의 나머지 footer는 0 padding이다.

manifest/hash 통과만으로 설치하지 않는다. provisioned 공개키로 image signature를 검사하고,
기기 long OK 후 동일 후보를 재검증하고 inactive OTA slot에 쓴다. 60초/500 loop의
정상 부팅 확인 전에는 rollback을 취소하지 않는다. 이전 firmware 복구도 VALID NOVA
slot만 허용한다. `scripts/build/release.py`는 이 계약으로 서명·manifest를 만들고 게시하지 않는다.
