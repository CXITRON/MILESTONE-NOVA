# MILESTONE NOVA 아키텍처

작성: 2026-09-23 · 수정: 2026-09-27 · Request U0001 재작업

## 범위와 빌드

현재 디렉터리와 기존 origin `CXITRON/MILESTONE-NOVA`가 신규 독립 프로젝트다.
`MILESTONE_Legacy/`는 읽기 전용 참고자료이며 include, 링크, 빌드 입력으로 사용하지 않는다.
로컬에서 확인한 Arduino-ESP32 3.3.11, LOLIN S3 설정(16 MB QSPI / 8 MB OPI
PSRAM), U8g2 2.36.19, Adafruit NeoPixel 1.15.5를 사용한다. staging sketch는
빌드 스크립트가 생성하며 원본은 `src/`의 실제 C++ 번역 단위로 유지한다.

## 소유권

| 소유자 | 책임 | 다른 실행 문맥과의 경계 |
|---|---|---|
| Arduino loop | 앱 상태, 입력, 미디어 clock, 가사 timeline, I2C, ADC, UI, LCD, LED, NVS | BLE에서 고정 길이 이벤트 수신 |
| SD worker (10 KiB stack) | SD SPI, 미디어 decode/index, catalog, 업로드/checkpoint, 로그 journal | 파일 작업 직렬화. 단일 asset 결과 대여/반환, 세대 번호로 오래된 결과 폐기 |
| Service worker (12 KiB stack) | HTTP/DNS, HTTPS 아트/업데이트, 환경 로그 큐 | App command/ack + 잠금으로 복사한 snapshot. SD는 Storage::execute로만 접근 |
| 로컬 OTA worker (12 KiB stack) | 서명 검증, OTA 수신/flash 쓰기 | atomic 상태·진행률. LCD/SD/NVS 접근 금지 |
| NimBLE host | 연결, 보안, GATT 요청/알림 | 콜백에서 파일·LCD·앱 상태 변경 금지. bounded queue만 사용 |

작업 코어를 고정하지 않는다. Arduino framework 외 추가 task는 SD·service·로컬 OTA 세 개다(OTA는 유효한 키/암호가 있을 때 생성). task를 반복 생성/삭제하지 않는다. BLE 객체는 수명 전체에 유지하고
연결/광고를 정지하는 방식으로 전원 전환한다. 보드 간 통신은 존재하지 않는다.

## 모듈 경계

- `board`: 단일 GPIO 정의, 컴파일 시 중복/예약 핀 검사.
- `core`: UTF-8, 날짜, battery LUT, focus timer. 설정 검증과 버튼 상태기계는
  각각 `settings`, `input`에 둔다.
- `display`: ST7789 SPI와 RGB565 canvas. U8g2 글꼴 decoder로 한/영 혼합 렌더링.
- `ui`: 모드/메뉴 탐색, CORE 9종·NOW 5개 레이아웃·MEDIA·설정·AP·업데이트·복구·진단.
- `media`: metadata, authoritative position anchor, pause/seek/resume/disconnect.
- `lyrics`: parser, binary-search timeline, provider/cache 경로, renderer 분리.
- `storage`: SD owner, 원자 교체·A/B catalog·재개 checkpoint·로그 journal,
  JPEG/BMP/MSM/RGB565 decoder와 시간 기준 frame 선택. UI 스레드는 SD를 열지 않는다.
- `sensors`: DS3231과 AHT10, I2C timeout, 비동기 AHT conversion, battery filtering.
- `network`: AP+STA, Wi-Fi 8 프로필/시험/NTP, 인증서 검증 HTTPS, 로컬 OTA.
- `artwork`: 기존 Worker MAC1 조회, 곡별 캐시/사용자 보호/한도 초과 시 LRU.
- `portal`: offline 자산과 API. 부팅 시 한 번 만든 worker에서 모든 HTTP 요청을 직렬 처리.
- `update`: NOVA manifest와 서명 후보/물리 확인 설치/이전 검증 partition 복구.
  ArduinoOTA와 SD 설치가 동시에 flash하지 않도록 App에서 상호 배제한다.
- `input`, `settings`, `lighting`, `power`, `logging`, `app`: 각 수명과 책임 분리.

## NOW와 가사

iOS AMS GATT entity notifications 또는 문서화된 BLE helper characteristic →
고정 길이 queue → MediaSession → 안정된 track key → local SD provider →
LrcParser → LyricsTimeline → NowPlayingScreen/LyricsRenderer.

곡의 artist/title/album/duration을 정규화하지 않은 UTF-8 바이트와 길이 구분자로
hash한다. helper의 명시적인 track key도 허용한다. 파일명은 고정 16진 key여서
경로 삽입이 불가능하다. `scripts/media/track_key.py`에서 동일 key를 계산한다.
AMS에는 artwork/lyrics 전송이 없다. 아트는 SD 우선 → 기존 Worker 자동 조회,
가사는 local LRC/포털 업로드 경로로 공급한다. 잘린 Track 속성은 Entity Attribute로
읽고, Remote Command notification의 지원 목록으로 기기 제어 UI/실행을 제한한다.
Android 등은 helper가 metadata/position을 전달해야 하며 OS 전체 플레이어
통합 앱이 이미 존재하는 것처럼 표시하지 않는다.

위치는 monotonic ms 기준 추정하고 pause에서는 고정한다. 새 위치는 즉시
anchor를 교체한다. 곡 변경은 가사/아트/위치 초기화, 연결 단절은 추정 정지.
타임라인은 이진 검색한다. LRC는 bounded line/text pool, 복수 timestamp,
offset, 중복 timestamp, UTF-8 오류, 빈 가사, 순수 텍스트를 처리한다.
온라인 provider 인터페이스는 SD와 분리하되 실제 서비스/credentials 없는
가짜 온라인 응답을 만들지 않는다.

## 화면과 메모리

240×320 RGB565 canvas 및 이전 전송 buffer를 PSRAM에 할당한다. 변경된 8행
strip만 SPI로 전송하며 한 loop에 전송량을 제한한다. 렌더링이 진행 중인 frame은
flush가 끝날 때까지 수정하지 않는다. LCD는 loop만 접근한다. text wrapping은
UTF-8 codepoint/글꼴 폭 기준이고 각 box 내부로 clip한다. 음악 가사 변경에도
LCD 전체 clear를 반복하지 않는다. PSRAM 할당 실패는 로그/LED로 명시하며
네트워크·버튼·기본 서비스가 재부팅 없이 살아 있도록 한다.

## AP Sync와 파일 교체

브라우저에서 원본을 160px JPEG sequence로 변환 → IndexedDB 256 KiB chunk →
AP HTTP CRC upload → SD `.part`/durable checkpoint → 전체 검증 → 원자 rename.
전송 재시도는 저장된 범위의 CRC를 다시 확인한다. 체크포인트 저장 실패 시 RAM offset도
이전 durable 값으로 되돌린다. 기존 정상 파일은 새 후보의 검증 전에 교체하지 않는다.

완료한 `/media/sync.njv`를 검증한 후 browser audio의 session/sequence/position/playing을
`Playback`에 전달한다. 2.5초 timeout, 오래된 sequence 거절, in-flight tick 종료 후
syncStop, AP BACK 종료, thermal stop을 각각 처리한다. 영상은 SD decoder가 읽고
오디오는 브라우저가 출력한다. 실시간 frame streaming/보드 간 SPI가 존재하지 않는다.
로컬 catalog 갱신은 Sync 종료 후 적용해 브라우저 시계를 덮어쓰지 않는다.
다른 모드로 전환하면 Sync 파일을 정리하며, SD 작업 대기/미탑재 상태에서도
매 loop의 재생 timeout 검사를 수행한다.
syncStart는 브라우저 변환 결과의 크기/전체 CRC도 검사한다. 전체 CRC는 기존
프레임 검증 순회에서 함께 계산하며 추가 SD 전체 읽기를 하지 않는다.
syncStop은 해당 세션만 종료하고 준비된 파일은 유지해 재연결을 허용한다.

설정 명령의 ACK 전에 최신 snapshot을 게시해 연속 HTTP 변경이 250ms 전의
설정을 기준으로 덮어쓰지 않게 한다. 아트/가사/보호 상태와 SD 재탑재가 바뀌면
SD worker의 asset revision을 갱신한다. App은 별도 세대 번호로 자료를 다시 읽고
과거 작업의 결과를 폐기한다. Artwork service도 자동 조회/저장 직전에 사용자
보호 상태를 확인한다.

## 오류와 저장소 복구

미디어/아트/LRC 오류는 해당 화면의 누락/오류 상태로 표현한다. 센서/SD 부재가
재부팅 이유가 되지 않는다. 포털의 명시적 repair는 열린 파일을 닫고 SD를 remount한 뒤
CRC catalog와 upload checkpoint를 복구한다. 환경 로그는 NLJ1 write-ahead journal로
완료한 append를 중복하지 않고 부분 append를 같은 offset에서 마친다. CRC가 손상된
journal은 조용히 버리지 않고 실패 상태와 원본을 보존한다.

A/B NVS 설정·자격증명·진단 이력은 CRC와 기록 순서를 확인한다. thermal 상태는
각 경계에서 5°C hysteresis를 적용하고 감속/정지 단계 동안 신규 업데이트/재생 제어를
제한한다. ambient AHT10 값과 내부 chip temperature는 구분한다.

## 전원과 업데이트

MENU 길게 누름 → OFF 요청 → OTA 중이면 거절 → 설정 저장 → SD 종료 확인 →
Wi-Fi/BLE 정지 → LCD sleep/backlight 0 → RGB/status 0 → OK 해제 대기 →
GPIO5 ext0 LOW wake → deep sleep. wake 시 RTC pad를 digital input으로 복구하고
모든 시작 시 눌린 버튼은 해제될 때까지 억제한다. peripheral 전원을 물리적으로
차단하는 회로가 없으므로 OFF는 0 μA 상태를 의미하지 않는다.
AP가 닫혔다는 상태만으로 service worker의 HTTPS 작업이 끝났다고 판단하지 않는다.
종료 요청에서는 신규 서비스 작업/환경 로그 접수를 막고, 진행 중 I/O 반환·AP 종료·
접수된 로그 drain 뒤의 별도 suspend ACK를 기다린 후 SD worker를 정지한다.
NVS 저장 실패로 OFF를 취소하면 service와 기존 AP를 다시 연다.
Artwork는 4초 HTTP I/O timeout에 더해 15초 경과를 수신 루프에서 검사해 작은
응답 조각만 계속 보내는 서버가 worker를 무기한 점유하지 않게 한다.

OTA는 사용자 활성화 시간 창 안에서만 수신한다. 비밀번호와 별도 공개키를
프로비저닝하고 Arduino 3.3.11 Update 서명 검증을 사용한다. private key는 PC에만
보관한다. 비활성 OTA 슬롯에 쓰고 검증 후 재부팅한다. 설치된 SDK의 rollback
설정을 확인했다. `verifyRollbackLater()`로 framework의 조기 승인을 미루고
60초/500회 loop와 heap 여유를 확인한 뒤 정상 이미지를 확정한다. 이 확인 전에
reset하면 rollback 대상이다. 실제 전원 차단/복구는 하드웨어 검증 항목이다.
legacy 다중 이미지, ZERO 승인, SAFE factory 프로토콜은 계승하지 않는다.

## 검증

실제 ESP32-S3 compile, host sanitizers, parser/UTF-8/날짜/설정/핀/버튼/배터리/
재생 및 타이머 상태 테스트. 실물 연결, TFT 색/방향/클럭, SD 제거, AMS pairing,
RF 공존, signed OTA 중단, 전류 및 장시간 soak는 별도 hardware checklist로 기록한다.

## 기술 근거

- [WEMOS S3 Pro](https://www.wemos.cc/en/latest/s3/s3_pro.html): flash/PSRAM와 SD 구성.
- [Apple AMS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Specification/Specification.html): entity subscription과 attribute, artwork/LRC 별도 경로.
- [Espressif LEDC](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html): Arduino 3 pin 기반 PWM API.
- [ESP32-S3 sleep](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html): ext0/RTC pad 동작.
- 설치된 core의 BLE, Update, ArduinoOTA 헤더/구현을 실제 API 기준으로 확인했다.
