# MILESTONE NOVA

LOLIN S3 Pro 한 대로 구동하는 탁상형 펌웨어, 버전 **0.1.0**.
240×320 ST7789V, 5버튼, 시계/날짜/D-Day/문구/집중 타이머, AHT10 환경 정보,
배터리 추정, NOW/앨범아트/동기 가사, SD 미디어, Wi-Fi/NTP, BLE, 서명 OTA,
설정 AP/오프라인 포털, AP Sync, Artwork Worker 200×200 자동 조회, 환경 CSV,
RGB 및 상태 LED, Deep Sleep OFF를 새 C++ 모듈로 구현한다.
기능별 구현·검증 근거는 [기능 대조표](docs/design/feature-parity.md)에 기록한다.

실제 ESP32-S3 빌드와 호스트 테스트를 수행했다. **실물 동작·장시간 안정성은 아직
검증하지 않았다.** 업로드 전에 [실물 점검표](docs/guides/hardware-validation.md)를 따른다.
기존 `MILESTONE_Legacy/` 없이 독립 빌드할 수 있다.

## 구성

```text
src/
  board/       확정 GPIO와 정적 충돌 검사
  core/        UTF-8, 날짜, 배터리 곡선, focus timer
  app/         초기화, 이벤트 연결, 수명 관리
  display/     ST7789V, RGB565 canvas, strip 전송
  input/       debounce/long/repeat, USB 설정 console
  settings/    schema/범위 검증, CRC/NVS 저장
  sensors/     DS3231, AHT10, battery ADC
  storage/     SD worker, 미디어 decoder, 업로드 checkpoint, catalog, 로그 journal
  media/       AMS/helper parser, BLE, playback clock, 파일 형식, JPEG decode
  lyrics/      provider/cache 계약, LRC parser/timeline/renderer
  ui/          공통 요소와 화면별 렌더링
  network/     Wi-Fi/AP/NTP, HTTPS, 로컬 서명 OTA와 부팅 확인
  artwork/     Artwork Worker 조회, 캐시 정책
  portal/      HTTP/DNS, 앱 명령 mailbox, offline 자산 제공
  update/      NOVA manifest, SD 후보 검증/설치/rollback
  lighting/    SK6812와 RED/GREEN manager
  power/       GPIO5 Deep Sleep
  logging/     UART0/USB 로그, CRC A/B 진단 이력
tests/         하드웨어 독립 logic/도구 검사
  render/      실제 C++ renderer와 이미지 검증
  storage/     실제 SD 처리 코드 + 파일/실패 주입 adapter
  portal/      로컬 API fixture와 브라우저 codec/IndexedDB 검사
assets/portal/ CDN 없는 HTML/CSS/JS 원본
scripts/
  build/       펌웨어 빌드, 포털 embedding, 독립 서명 release 생성
  test/        호스트 검사와 화면 검증
  media/       key/이미지·영상 변환/BLE helper
  ids/         업무 문서 ID 확인
  request/     Request 생성과 queue
data/config/   SD 설정 예시
docs/
  design/      선행 분석과 아키텍처
  guides/      데이터 프로토콜과 실물 검증
  codex/       Request/Report/업무 템플릿
```

[선행 분석](docs/design/legacy-analysis.md), [아키텍처와 소유권](docs/design/architecture.md),
[BLE·파일 프로토콜](docs/guides/protocol.md)을 참고한다. 구현 코드를 `.inc`로 공유하거나
다른 `.cpp`를 include하지 않는다. `src/main.cpp`는 앱의 setup/loop만 연결한다.

## 하드웨어 / 최종 GPIO

LOLIN S3 Pro: ESP32-S3, **16 MB QSPI flash / 8 MB OPI PSRAM**.
ST7789V 2.0인치 SPI RGB565 패널은 MISO를 연결하지 않는다.
아래 표의 단일 코드 기준은 `src/board/Board.h`이다.

| GPIO | 기능 |
|---:|---|
| 1 | RED status LED, 1 kΩ 직렬, active HIGH |
| 2 | 외부 SK6812 3535 RGB(MINI-HS) 5개, 74HCT125N + 510Ω |
| 3 | 배터리 ADC, 보드 내장 100 kΩ / 100 kΩ divider (JP2 `BAT_AD` 연결 필요) |
| 4 | RESERVED |
| 5 | OK / Deep Sleep wake, INPUT_PULLUP |
| 6 | LCD MOSI |
| 7 | LCD SCK |
| 8 | GREEN status LED, 1 kΩ 직렬, active HIGH |
| 9 | I2C SDA |
| 10 | I2C SCL |
| 11 | SD MOSI |
| 12 | SD SCK |
| 13 | SD MISO |
| 14 | RESERVED |
| 15 | LCD CS |
| 16 | LCD RESET |
| 17 | LCD D/C |
| 18 | LCD backlight PWM |
| 19 | native USB |
| 20 | native USB |
| 21 | RESERVED |
| 38 | onboard RGB, 부팅 시 OFF |
| 39 | BACK |
| 40 | PREV |
| 41 | NEXT |
| 42 | MENU |
| 43 | UART0 TX debug |
| 44 | UART0 RX debug |
| 46 | onboard SD CS |
| 47 | RESERVED |
| 48 | RESERVED |

GPIO0/45 strapping 핀과 GPIO26–37 flash/PSRAM 영역은 사용하지 않는다.
핀 중복과 예약 GPIO 사용을 컴파일 시 검사한다. LCD는 FSPI, SD는 HSPI로
서로 다른 controller와 GPIO를 사용한다. 기본 LCD **20 MHz**, SD **10 MHz**,
공유 I2C **100 kHz**다. 긴 LCD 배선에서 검증 후에만 속도를 높인다.
DS3231(0x68)/AHT10(0x38) 및 I2C pull-up은 3.3 V 기준이다.
배터리 percentage는 부하/온도에 따라 달라지는 전압 기반 추정치이며 fuel gauge가 아니다.

### 전원 및 외장 부품 배선

LOLIN S3 Pro 공식 회로도 V1.0.0 기준이다. 펌웨어는 아래 구조를 전제로 하며
배선이 달라지면 `src/board/Board.h`와 이 절을 함께 수정한다.

| 부품 | 연결 |
|---|---|
| 배터리 | 1셀 Li-Po/Li-ion을 보드 PH2.0(P3)에 연결. 충전은 보드 TP4054가 담당 |
| 배터리 측정 | 보드 내장 VBAT─100 kΩ─AD_BAT─100 kΩ─GND, 100 nF. AD_BAT는 솔더 점퍼 JP2(`BAT_AD`)를 거쳐 GPIO3에 연결된다. 외부 divider를 추가하지 않는다. 회로도 기호상 JP2는 open으로 보이므로 실물 패드가 떨어져 있으면 납땜으로 연결한다. 미연결 시 배터리 표시/경고가 동작하지 않는다 |
| 5V_AUX | DM13B 자동 buck-boost 5 V 모듈. 입력 VI+ = 보드 `VIN`, VI− = GND, 입출력에 각각 100 µF. `VIN`은 USB 연결 시 VBUS(다이오드 경유), 배터리만 있을 때 VBAT(MOSFET 경유)이므로 3.0–5 V 입력을 모두 처리하는 buck-boost를 사용한다 |
| 74HCT125N | VCC = 5V_AUX, GND, VCC–GND 100 nF. 4번 채널만 사용: 4A = GPIO2, 4Y → 510 Ω → LED1 DIN, 4OE = GND. 미사용 1A–3A는 GND, 1OE–3OE는 VCC(출력 high-Z) |
| SK6812 3535 RGB ×5 | 3색 RGB(SZH-LD159, MINI-HS). VDD = 5V_AUX, LED마다 VDD–GND 100 nF, LED1 DOUT → LED2 DIN → … → LED5. 펌웨어는 `NEO_GRB + NEO_KHZ800`. RGBW 제품으로 바꾸면 `src/lighting/Lights.h`를 수정한다 |
| LCD | 보드의 TFT 전용 SH1.0 커넥터(J2)는 사용하지 않고 헤더에 외부 배선한다. VCC = 3V3, GND, DIN→6, SCK→7, CS→15, RST→16, D/C→17, BL→18 |
| 버튼 ×5 | P2285 tactile. 한쪽 = 해당 GPIO, 반대쪽 = GND 공통(INPUT_PULLUP, 눌림 LOW) |
| DS3231 / AHT10 | VCC = 3V3, GND, SDA→9, SCL→10. 보드에 I2C pull-up이 없으므로 모듈 내장 pull-up을 사용한다 |
| RED / GREEN LED | GPIO1 / GPIO8 → 1 kΩ → LED → GND. 미장착이어도 펌웨어 동작에 영향이 없다 |

조립 순서: 74HCT125N과 DM13B를 LOLIN 장착 전에 먼저 납땜한다. 첫 전원 인가 전
극성·단락을 확인하고, USB만 연결한 상태에서 74HCT125N 14번(VCC)–7번(GND)이 5 V인지
측정한 뒤 LED와 보드를 연결한다.

Deep Sleep(OFF)은 5V_AUX를 차단하지 않는다. DM13B는 `VIN`에 상시 연결되어 있어
OFF 중에도 DM13B 대기 전류, SK6812 5개의 대기 전류(꺼진 상태에서도 소모),
74HCT125N 전류가 배터리에서 흐른다. 차단이 필요하면 EN 핀이 있는 모듈과 RTC GPIO
(예: GPIO4)를 사용하도록 회로와 `Board.h`·OFF 처리를 함께 변경해야 한다.

## 빌드 / 테스트 / 최초 업로드

검증 환경: Arduino-ESP32 **3.3.11**, U8g2 **2.36.19**, Adafruit NeoPixel **1.15.5**,
Arduino CLI, host GCC/Python3/Node.js/libjpeg 개발 패키지. Adafruit GFX/별도 TFT·RTC·AHT library는 필요하지 않다.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install 'U8g2@2.36.19' 'Adafruit NeoPixel@1.15.5'
./scripts/build/firmware.sh
./scripts/test/run.sh
./scripts/test/preview.sh
```

CLI가 PATH에 없으면 `ARDUINO_CLI=/path/to/arduino-cli ./scripts/build/firmware.sh`를 사용한다.
이 환경의 Arduino IDE 번들 CLI 경로도 자동 탐색한다. 추가 flags는 스크립트 뒤에
전달한다. 소스는 `build/sketch/Nova/src`에 staging하며 산출물은
`build/firmware/Nova.ino.bin`, `.elf`, `.merged.bin`이다. 두 OTA slot은 각 6 MiB,
PSRAM은 LOLIN S3 board profile의 OPI 설정을 사용한다. `build/`는 Git에서 제외한다.

`scripts/test/run.sh`는 ASan/UBSan을 사용한다. ptrace 기반 실행 환경에서는 LeakSanitizer가
동작하지 않아 기본 `detect_leaks=0`이며 지원되는 호스트에서
`ASAN_OPTIONS=detect_leaks=1 ./scripts/test/run.sh`로 추가 검사할 수 있다.
`preview.sh`는 제품의 C++ renderer로 화면·레이아웃·진단·대기/오류 상태 63개 장면을 그린다.
`build/preview/index.md` 또는 `index.html`에서 이름과 분류별로 전체 화면을 볼 수 있다.
개별 `screen-*.png`는 원본 240×320 픽셀이며, `screens*.png`는 9개씩 묶은 비교 이미지다.
날짜·음악·센서·아트는 예시 데이터이며 실물 LCD나 동작 애니메이션의 캡처는 아니다.
필요하면 `U8G2_CLIB=/path/to/U8g2/src/clib`를 지정한다.
펌웨어 산출물과 Python `cryptography`가 있으면 호스트 검사에 RSA-2048/4096
서명·변조 거절 검사도 포함된다. SDK 위치는 `NOVA_CORE`로 지정할 수 있다.
필요 파일/의존성이 없으면 서명 검사는 skipped로 표시된다. 최종 검증에서는 skip 없이 실행했다. 테스트 키는 임시
디렉터리에서만 생성하고 삭제하며, 실물 OTA 전송 검증을 대신하지 않는다.

최초 USB 업로드 예시(포트는 실제 보드에 맞춘다):

```sh
arduino-cli upload --fqbn esp32:esp32:lolin_s3:USBMode=hwcdc,CDCOnBoot=cdc \
  --port /dev/ttyACM0 --input-dir build/firmware build/sketch/Nova
```

본 구현 작업에서는 실제 장치에 flash하지 않았다. 로그는 USB CDC 및
GPIO43/44 UART0, 115200 baud로 출력한다. USB console은 개행 단위 명령을 받는다.

브라우저 변환 검사는 `python3 tests/portal/serve.py` 실행 후
`http://127.0.0.1:8765/tests/browser.html`에서 실행한다(ffmpeg 필요).
같은 서버의 `/`는 API fixture로 화면을 확인하는 개발용 페이지이며 장치 연동 시험이 아니다.

## 버튼

| 조작 | 동작 |
|---|---|
| MENU 짧게 | CORE / MEDIA / NOW, 기기 설정, 설정 AP, 업데이트, 복구, 진단, 재시작, OFF 메뉴 |
| 메뉴 PREV/NEXT, OK, BACK | 선택 이동, 적용, 취소. 모드 변경에 재부팅하지 않음 |
| CORE PREV/NEXT | 설정된 순서와 사용 화면 mask로 이동. 기존 7개 조합 + Focus/환경 |
| BACK 짧게 | 편집/메뉴 취소. AP가 열려 있으면 AP와 Sync 종료 |
| MENU 0.9초 이상 | OFF 요청; 업데이트/새 이미지 부팅 확인 중에는 거절 |
| Focus OK | 시작/일시정지/재개; 길게 누르면 기본 시간으로 reset |
| NOW OK | iPhone이 지원하는 경우 play/pause; 길게 누르면 레이아웃 변경 |
| NOW PREV/NEXT 짧게 / 길게 | 레이아웃 / 지원되는 이전·다음 곡 명령 |
| MEDIA PREV/NEXT, OK | 사용 설정된 이전·다음 파일 / 로컬 영상 재생·정지 |
| AP Sync | 브라우저 오디오의 재생·정지·seek를 따름. 기기 OK로 음원을 조작하지 않음 |
| System OK 또는 길게 PREV/NEXT | 12개 진단 페이지 이동 |
| 기기 설정 PREV/NEXT, OK | 항목 이동, 편집 시작/종료, 값 변경 |
| 업데이트 OK / 길게 OK | 확인·다운로드·후보 검증 / 검증된 후보 설치. 복구 항목에서는 이전 slot 선택 |
| OFF 상태 OK | wake; 해제될 때까지 추가 입력 억제 |

명령 전송 자체를 실제 재생 변화로 표시하지 않는다. 새 authoritative 상태가 와야
NOW가 바뀐다. 일반 버튼은 active LOW, 30 ms debounce, 900 ms long press,
필요한 PREV/NEXT repeat 180 ms다.

## 설정과 Wi-Fi

MENU → 설정 AP → 화면에 표시된 `MILESTONE-NOVA-SETUP`에 연결 →
`http://192.168.4.1/`. HTML/JS/CSS와 GIF decoder는 펌웨어 안에 들어 있어 외부 CDN이
필요 없다. 기본 AP 암호는 매번 새 8자리다. 고정 암호 및 사용자가 확인한 개방형 모드를
지원한다. 암호는 기기 화면에만 표시하며 API로 되돌려 주지 않는다.

연결 탭에서 비동기 검색 후 Personal/Open/PEAP를 시험한다. 15초 안에 연결되고
2초 이상 유지된 경우 최대 8개 프로필에 저장한다. WPA3 전용과 CA 검증이 필요한
Enterprise 구성은 지원하지 않는다. AP가 열려 있는 동안 NOW BLE는 중단하며,
BACK 또는 포털의 AP 닫기를 누르면 선택한 모드로 돌아간다.

`data/config/device.ini`는 최초 설정을 위한 SD `/config/device.ini` 예시다.
유효한 NOVA NVS 설정이 있으면 이 파일은 다시 적용하지 않는다.
`secrets.example.ini`를 채워 `/config/secrets.ini`로 복사하면 Wi-Fi/AP/OTA를
프로비저닝할 수 있다. 이 자격증명 파일과 `/config/ota_public.pem`은 부팅마다 읽는다.
프로비저닝 후 SD의 자격증명 파일을 제거하면 이후 포털 변경값을 NVS에 유지한다.
설정 기본값은 자격증명을 유지한다. 기기 초기화는 NOVA NVS를 지우며 SD 파일은
보존하므로, SD에 남긴 프로비저닝 파일은 다음 부팅에 재적용된다.
이전 설정 가져오기는 같은 칩의 `milestone` schema 1~12 namespace를 읽기만 한다.

USB console에서도 설정할 수 있다.

```text
set lcd_brightness=160
set rgb_brightness=24
set heartbeat_brightness=24
set lcd_hz=20000000
set dday=2027-01-01
set message=오늘도 한 걸음
set timezone=KST-9
set focus_seconds=1500
set battery_gain=1.000
set battery_offset=0.000
wifi MyNetwork|my-network-password
time 1790121600
status
ota
ota-close
sleep
```

시간 명령 값은 UTC Unix seconds다. RTC에도 UTC를 저장하고 화면에서 POSIX TZ를
적용한다. NTP는 Wi-Fi 연결 뒤 비동기로 시작한다. Wi-Fi 신규 연결은 15초 제한과
설정된 상한까지의 재시도 간격을 쓰며 BLE 연결 중 새 association은 미룬다.
SSID/비밀번호 변경은 BLE/OTA 연결을 끝낸 뒤 한다. 비밀번호는 console에 echo하지 않는다.
설정은 schema/CRC/범위 검사 후 사용하며 잘못된 데이터는 기본값으로 복구한다.

## SD / NOW / 가사 / 앨범아트

FAT32 카드의 구조:

```text
/config/device.ini
/config/secrets.ini
/config/ota_public.pem
/lyrics/<16자리-track-key>.lrc
/artwork/<16자리-track-key>.nvi
/media/photo.nvi
/media/clip.njv
/media/photo/old.bmp
/media/video/old.mvj
/media/catalog.a, catalog.b       # 자동 관리
/media/.transfer, *.part         # 진행 중 업로드
/logs/YYYY-MM-DD.csv
/update/candidate.bin
```

iPhone은 `NOVA`에 BLE pairing하고 Apple Media Service에서 제공하는 title, artist,
album, state, elapsed, duration, player/queue 정보를 받는다.
잘린 Track 문자열은 Entity Attribute read로 보완한다(문자열당 최대 240 UTF-8 bytes).
Remote Command 지원 목록과 GATT 상태가 유효할 때만 기기 제어를 허용한다. AMS가 없으면 기다림 상태를 유지한다.
Android/기타 플레이어는 [helper protocol](docs/guides/protocol.md)로 snapshot을 보내야 한다.
`scripts/media/ble_helper.py`는 제공받은 JSON을 BLE로 전달한다. 스마트폰의 모든 플레이어에
접근하는 완성된 native companion 앱은 포함하지 않는다. BLE 연결 자체로 artwork나
가사를 획득했다고 표시하지 않는다.

트랙 key는 USB `status` 로그에서 확인하거나 다음과 같이 계산한다.

```sh
python3 scripts/media/track_key.py --artist '아티스트' --title '곡 제목' --album '앨범' --duration-ms 231000
```

가사 파일은 UTF-8 LRC(최대 32 KiB / 512 entries / 1024 bytes per line):

```text
[ar:직접 작성한 예시]
[offset:0]
[00:01.00]첫 번째 예시 문장
[00:04.250][00:08.50]English and 한글
[00:12.00]
[00:15.00]마지막 예시 문장
```

초 단위/소수 1~3자리, 여러 timestamp, 파일 전체 offset, CRLF/LF, metadata,
잘못된 줄, 중복 timestamp를 처리한다. 같은 시각은 마지막 entry를 선택한다.
빈 timed entry는 instrumental로 표시한다. timestamp 없는 파일은 **UNSYNCED**로
표시한다. 비동기 가사는 가사 전용 화면에서 첫 4줄, 통합 화면에서 첫 3줄을 보여 준다.
재생 위치가 없으면 가사를 임의로 진행시키지 않는다. play 중 monotonic 추정,
pause 고정, seek 즉시 보정, 곡 변경 시 reset, binary search로 줄을 선택한다.

로컬 LRC가 없고 `lyrics_view`가 켜져 있으면 Wi-Fi 연결 후 기존 Worker의
`POST /v1/lyrics`로 온라인 동기화 가사를 조회한다. Worker는 LRCLIB 공식 API에서
곡 제목·아티스트·앨범·재생 길이를 조회하고 유효한 LRC만 반환한다.
받은 가사는 SD `/lyrics/<key>.lrc`에 저장해 다음부터 오프라인으로 사용한다.
기존 파일은 빈 파일이라도 자동으로 덮어쓰지 않는다. SD 없음/저장 실패 시에는
현재 곡의 RAM 결과를 표시한다. 곡이 바뀐 뒤 도착한 결과는 화면에 적용하지 않는다.
온라인 조회는 곡 자산을 읽을 때 한 번 시도한다. 실패한 곡의 재시도는 곡 재선택이나
포털의 현재 곡 재읽기(`artRefresh`)로 할 수 있다. AP 사용/OFF 중에는 새 조회를 시작하지 않는다.
`artwork_auto`와 가사 조회는 독립이며, 자동 가사 조회는 `lyrics_view`를 따른다.
서버 배포 전에는 가사 경로가 없을 수 있다. [API와 제한](docs/guides/protocol.md#온라인-가사-worker)을 참고한다.

NOW 레이아웃은 0~5번이다. 0은 큰 아트+제목/아티스트, 1은 작은 아트+상세,
2는 텍스트, 3은 큰 아트+제목/앨범, 4는 가사, **5는 아트+곡 정보+가사+진행 바**다.
0/3의 제목과 아티스트/앨범은 각각 한 줄을 쓴다. 5의 아트는 88×88이고 아래에 가사 3줄이 있다.
짧은 PREV/NEXT 또는 길게 OK로 전환한다. 포털 `now_layout`에서도 고를 수 있다.
`scroll`이 켜져 있고 글자가 영역보다 길면 제목·아티스트·앨범 marquee는 양 끝에서
2초씩 멈추며 가로로 왕복한다(`scroll_speed`, 기본 24 px/s). 모든 텍스트에 적용되는 것은
아니다. 2번의 큰 제목과 가사는 줄바꿈/영역 자르기를 사용한다.

```sh
# P6 PPM은 Python 표준 library만 필요. PNG/JPEG에는 Pillow가 필요하다.
python3 scripts/media/media_pack.py cover.ppm --side 200 --output <track-key>.nvi
python3 scripts/media/media_pack.py frame-*.ppm --fps 5 --output clip.nvv   # 기본 240
```

NOW 큰 아트 레이아웃은 **200×200 원본을 172×172로 면적 평균 축소**해 텍스트 두 줄의
공간을 확보한다. 작은 아트 레이아웃(88×88)도 면적 평균으로 줄인다.
앨범아트는 SD 캐시(`/artwork/<key>.nvi`, NVI1 200×200)를 우선한다.
없으면 MILESTONE Artwork Worker(MILESTONE-Core 저장소 `services/artwork-worker`)의
`/v3/artwork`에서 baseline JPEG 200×200을 받아 기기에서 디코딩한다(요청 전체 15초 제한).
Worker 갱신 배포 전에는 같은 경로가 88×88 MAC1을 돌려주므로, 길이·CRC를 검증한 뒤
200×200 버퍼로 부드럽게 확대한다. 이전 펌웨어가 저장한 160×160 캐시도
읽어서 200×200 버퍼로 확대한 뒤 레이아웃 크기로 축소해 표시한다. AMS가 이미지를 전송하는 것은 아니다. 기본 캐시 한도 2 GiB를 넘을 때만 오래된
자동 이미지를 정리한다. 기본 여유 공간 1 GiB 미만에서는 추가 저장을 거절하며
공간 확보를 위해 고정/사용자 이미지를 지우지 않는다.
포털에서 검색·미리보기·사용자 이미지/LRC 업로드·고정·차단·재조회·삭제할 수 있다.
현재 곡의 저장 자료가 바뀌면 다시 읽어 화면에 반영하고, 변경 전 조회 결과는 폐기한다.
재조회가 실패하면 기존 이미지를 유지하며 대기 중이던 자동 요청도 사용자 보호 상태를 확인한다.
삭제한 키는 MISSING으로 남겨 자동으로 다시 내려받지 않는다. 차단은 자동 조회를
멈추며 이미 저장된 이미지는 계속 표시한다.

## 로컬 MEDIA와 AP Sync

미디어 탭에서 사진/GIF/영상을 선택하면 브라우저가 가운데 정사각형을 **240×240**으로
변환한다. MEDIA 화면은 이 프레임을 상태 표시줄 아래 화면 폭 전체에 표시한다.
사진은 NVI1, 영상/GIF는 JPEG 프레임 NJV1이다. 원본이 NVI1/NVV1/NJV1(240 또는 이전 160),
기존 MVJ1(128×128 JPEG)/MSM1(흑백 또는 RGB332)/24-bit BMP이면 직접 업로드할 수 있다.
240이 아닌 파일은 표시 때 240으로 확대한다(사진은 bilinear, 영상은 nearest).
목록은 최대 64개이며 이름·순서·사용 여부·표시 시간·삭제·전체 삭제·재스캔을 지원한다.
개별 표시 시간 0은 화면 설정의 `media_seconds`를 따른다. 자동 전환은 설정 시간과
애니메이션 종료/반복 경계를 함께 만족해야 하므로 영상 중간에 임의로 넘기지 않는다.

브라우저는 최대 256 KiB 단위로 IndexedDB에 자료를 보관하고 SD 전송을 재개한다.
각 chunk의 CRC와 체크포인트, 전체 파일 형식·프레임 검증이 끝나야 기존 파일을 교체한다.
`*.nix`는 SD worker가 생성하는 탐색 인덱스다. `.part`/catalog/인덱스를 직접 편집하지 않는다.
파일은 2 GiB 미만, NOVA/MVJ 영상은 최대 6시간·30 FPS, JPEG 프레임은 최대 48 KiB다.
240×240 영상의 실제 FPS는 LCD 20 MHz에서 약 12~15로 추정하며 실물 측정 대상이다. CLI의 raw `media_pack.py`는 보수적인
1~10 FPS/512 MiB 출력 한도를 그대로 사용한다.

AP Sync 탭은 **기기의 SD 영상 + 브라우저의 원본 오디오**를 동기화한다.
원본 영상 선택 → SD 동기 영상 준비·업로드 → 동기 재생 연결 → 브라우저 오디오 재생.
브라우저가 기준 시각이며 pause/seek/재생 상태를 약 250 ms마다 보낸다.
기기는 session/sequence가 오래된 명령을 거절하고 2.5초 동안 신호가 없으면 정지한다.
브라우저 탭 숨김·통신 오류도 오디오를 정지한다. 동기 종료는 연결을 해제하고 준비한
SD 영상은 유지해 다시 연결할 수 있다. BACK/AP 종료 또는 모드 변경 시 완료된 임시
Sync 영상을 정리하고 일반 미디어 업로드 체크포인트는 유지한다. 연결할 때 브라우저의
변환 파일 크기·전체 CRC와 SD 파일을 대조하므로 실패한 새 업로드 뒤 이전 영상에
잘못 연결하지 않는다. 보드 자체 오디오 출력은 없다.

브라우저 저장소를 열지 못해도 포털의 설정·연결·진단은 사용할 수 있다.
미디어/펌웨어 파일 준비·전송은 IndexedDB를 사용할 수 있는 일반 브라우저에서 진행한다.

## 서명 OTA / 복구

로컬 Arduino OTA는 사용자 활성화 후 10분 동안만 수신한다.
전송 중 BLE는 연결/광고를 중지하고 객체 수명은 유지한다. OTA worker가 파일을
수신하며 UI/입력은 계속 실행한다. 한 전송은 120초, 무응답은 짧은 timeout으로
제한한다. SD 없이도 이미 저장된 Wi-Fi/password/public key로 OTA가 가능하다.

RSA-2048 이상의 공개키와 12자 이상 OTA password가 없으면 OTA가 비활성화된다.
`/config/ota_public.pem`에는 **공개키만** 놓는다. private key는 PC에 별도 보관한다.
공개키를 SD에서 프로비저닝하는 모델이며 secure boot/flash encryption은 설정하지 않았다.

```sh
# NOVA_CORE는 설치된 Arduino-ESP32 3.3.11 디렉터리
export NOVA_CORE="$HOME/.arduino15/packages/esp32/hardware/esp32/3.3.11"
python3 "$NOVA_CORE/tools/bin_signing.py" --generate-key rsa-2048 --out /safe/nova-private.pem
python3 "$NOVA_CORE/tools/bin_signing.py" --extract-pubkey /safe/nova-private.pem --out /safe/ota_public.pem
python3 "$NOVA_CORE/tools/bin_signing.py" --bin build/firmware/Nova.ino.bin \
  --key /safe/nova-private.pem --out build/firmware/Nova-signed.bin
# 기기 Settings의 Signed OTA 또는 USB console의 ota로 수신 창을 연 뒤:
python3 "$NOVA_CORE/tools/espota.py" -i <device-ip> -a "$NOVA_OTA_PASSWORD" \
  -f build/firmware/Nova-signed.bin
```

**같은 3.3.11 core의 `espota.py`**를 사용한다. 이 버전의 인증은 PBKDF2/SHA256이며
서명은 RSA/SHA256이다. 전송 MD5만으로 펌웨어를 신뢰하지 않는다.
서명 검증을 통과해야 비활성 slot을 다음 부팅 대상으로 지정한다.
새 이미지의 초기 확인을 미루고 60초/500회 이상 loop와 최소 heap 여유를 확인한 뒤
정상 확정한다. 그 전에 reset하면 bootloader rollback 대상이다. optional SD/RTC/AHT
부재는 확정 실패 이유로 삼지 않는다. 실물 전원 차단·rollback 시험은 별도로 필요하다.

인터넷 업데이트는 `src/network/Endpoints.h`의 NOVA 전용 manifest를 확인하고,
명시된 크기/SHA256과 RSA 서명을 검증한 뒤 SD 후보를 준비한다. 직접 올린 서명 이미지도
같은 검증을 거친다. 칩 ID가 ESP32-S3이고 application descriptor의 제품명이
`MILESTONE-NOVA`여야 한다. 기기에서 OK를 길게 누르기 전에는 설치하지 않는다.
설치 직전 크기·hash·서명을 다시 검증한다. 복구 메뉴는 이전의 VALID 상태 NOVA slot만
선택한다. legacy MAIN/ZERO 이미지와 키, 릴리스 카탈로그는 사용하지 않는다.

릴리스 산출물 준비(생성만 하며 게시·flash하지 않음):

```sh
python3 scripts/build/release.py --key /safe/nova-private.pem \
  --url https://raw.githubusercontent.com/CXITRON/MILESTONE-NOVA/main/releases/nova-0.1.0.bin \
  --output-dir build/release/0.1.0
```

출력 signed `.bin`과 `stable.json`은 키·서명·제품 descriptor를 검증한 파일이다.
인터넷 업데이트를 실제 배포에 사용하려면 이 파일들을 설정된 NOVA 저장소에 게시해야 한다.
이번 작업에서는 공개 릴리스를 게시하지 않았다. 미게시/접속 실패를 업데이트 가능 상태로
표시하지 않는다. SD 후보 및 로컬 OTA 경로는 공개 릴리스 없이 사용할 수 있다.

## 검증 범위와 제한

- 실제 LCD 방향/색상/10 cm 배선 clock, RF 공존, SD 제거, Deep Sleep 전류,
  ADC 보정, iPhone pairing/reconnect/원격 제어, OTA 전원 차단 및 24~72시간 soak는 미검증.
- 호스트 SD 검사는 실제 처리 코드에 파일/오류 adapter를 붙여 실행하며, SPI/FreeRTOS
  스케줄링 또는 ESP JPEG decoder의 실물 동작을 대신하지 않는다.
- AP Sync의 오디오는 원본을 해석하는 브라우저에서 난다. 코덱/백그라운드 제한과
  실제 Wi-Fi 지연에 따른 동기 오차는 사용하는 iPhone/브라우저에서 측정해야 한다.
- 온라인 가사는 Worker 배포와 인터넷 연결이 필요하며 공급원에 없는 곡은 표시하지 못한다.
  동기화 정보 없는 온라인 가사는 내려받지 않는다. 로컬 LRC는 그대로 사용할 수 있다.
  v3 JPEG 200 응답은 로컬 Node에서 실제 Deezer/Apple 조회와 Wasm MozJPEG 변환을 확인했으나
  Worker를 아직 배포하지 않았다. 배포 전에는 88×88 MAC1 확대본이 표시된다. ESP32 TLS/RF 공존은 별도 검증한다.
- 배터리는 전압 추정이다. 충전 상태 입력이 없어 충전 여부를 표시하지 않는다.
- 글꼴 미수록 한글/기타 문자/emoji는 `?`로 대체한다.
- RGB 음악 효과는 수신한 재생 상태 기반이며 오디오 분석이 아니다.
- LCD MISO가 없어 초기 명령 전송 성공만으로 패널 연결을 증명하지 못한다.
- Deep Sleep은 외부 5V_AUX(DM13B)와 SD/RTC 전원을 물리적으로 차단하지 않는다.
  OFF 중 소모 전류는 [전원 및 외장 부품 배선](#전원-및-외장-부품-배선)을 참고한다.
