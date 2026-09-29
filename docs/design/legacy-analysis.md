# 기존 MILESTONE 선행 분석 및 기능 inventory

2026-09-23 선행 분석 · 2026-09-24 U0001 재작업 반영.
초기 분석의 자동 아트/기존 미디어 형식/설정 import 제외 판단은 사용자 지시와
기존 코드 대조에 따라 철회했다. 아래 표는 수정된 범위다. 참고 루트: `MILESTONE_Legacy/MILESTONE_Core`.
원본 파일을 수정하거나 새 프로젝트에서 include하지 않는다.

## 확인한 구현

| 기능 | 확인한 파일 | NOVA 판단 |
|---|---|---|
| NOW | CoreDisplay.inc, CoreBluetooth.inc, v5/.../MilestoneV5Now.cpp | metadata 누락 fallback, 재생률/위치 anchor 개념 유지; 240×320 재설계 |
| BLE | CoreBluetooth.inc, v5/MilestoneV5Zero/V5AmsRuntime.inc | iOS AMS, peripheral 연결 후 같은 연결의 GATT client; Classic 사용 안 함 |
| 앨범아트 | CoreArtwork.inc, v5/MilestoneV5Main/V5Artwork.h | gateway/iTunes/MusicBrainz → 검증된 RGB565/JPEG → SD cache. 기존 Artwork Worker 자동 조회와 MAC1 검증, local cache 및 관리 포털 복원 |
| SD media/video | CoreMedia.inc, MilestoneV5Video.h/.cpp, V5SyncMedia.h, README | BMP, MVJ1 JPEG, MSM1, 시간 기준 frame skip. BMP/MVJ1/MSM1 reader와 NOVA NVI1/NVV1/NJV1, SD 기반 AP Sync 재설계 |
| D-Day/시계/문구 | CoreDisplay.inc, V5CoreViews.h | civil date 계산과 한글 문구 유지, 유효하지 않은 시간은 미설정 표시 |
| 집중 타이머 | 기존 화면·기능 정의 조사 | v5의 명확한 독립 focus timer 경로를 확인하지 못함. 새 monotonic 상태기계로 작성 |
| RTC | MilestoneV5Rtc.cpp, V5Hardware.h | DS3231 BCD/OSF와 날짜 유효성 검사, NTP fallback; 새 UTC 저장 계약 |
| 환경 센서 | V5Environment.h | 기존 AHT20 7-byte/CRC 프로토콜을 복사하지 않고 요청한 AHT10 6-byte 프로토콜 사용 |
| 배터리 | 기존 pin map/하드웨어/화면 조사 | 요청한 GPIO3 divider용 코드와 표시는 새 ADC 보정/LUT 설계; 기존에 동일 회로를 검증한 것으로 보지 않음 |
| 버튼/탐색 | CoreRuntime.inc, MilestoneV5Input.h, Main.ino | debounce 및 press-release 의미 유지. CORE/MEDIA/NOW 메뉴/OK 확인/BACK 취소 및 wake 억제/long/repeat 처리 |
| splash | CoreDisplay.inc, Main.ino renderBootSplash | 새 해상도에 맞는 NOVA splash, 부팅 후 3초 표시 |
| 설정 | CoreConfig.inc, MilestoneV5Settings.cpp, V5CoreViews.h | NVS, schema와 validation 개념 유지; schema 1~12 읽기 전용 import, NOVA CRC A/B 저장 |
| Wi-Fi | CoreNetwork.inc, V5Radio.h, V5Network.h, README | 비동기 scan/시험/최대 8 프로필/PEAP, AP 포털과 bounded 재시도/NTP, 별도 credentials. MAIN/ZERO radio 배정 제거 |
| OTA | CoreUpdate.inc, V5BundleUpdate.h, release-v5.sh | 비활성 slot, integrity/signature 유지. NOVA 독립 manifest/SD 후보/물리 확인/이전 검증 slot 복구; 다중 보드 bundle/13자산/SAFE 제거 |
| 전원 | CoreRuntime.inc, Main.ino/기존 기능 문서 | 과거 재시작/안전모드 UX와 분리해 새 GPIO5 deep sleep OFF 구현 |
| 로그/진단 | CoreDiagnostics.cpp/.inc, MilestoneV5Diagnostics.h, Main.ino | prefix, reset reason, heap/stack/loop 측정. 반복 loop spam 제거 |
| 오류/장시간 | MILESTONE_PROJECT_CONTEXT.md, v5 WORK_CHECKPOINT_20260913_* | optional failure와 실제 검증 범위를 구별. 확인된 원인만 계승 회피 |

## 구조와 실패 분석

1. v3의 `.inc`는 한 sketch의 전역 config, display, BLE/OTA/SD 상태를 공유한다.
   v5는 MAIN/ZERO/SAFE와 header에 구현된 runtime까지 결합된다. 파일을 나누는
   것만으로 수명/동시성 경계가 생기지 않으므로 새 C++ 모듈을 작성한다.
2. `WORK_CHECKPOINT_20260913_LAG.md`는 66개 artwork cache에서 반복 fopen/stat/
   폴더 재검색을 지연 원인으로 확인했다. 새 코드는 곡 key에 해당하는 파일만
   읽고 파일 I/O를 UI loop 밖으로 옮긴다.
3. `WORK_CHECKPOINT_20260913_HOTFIX.md`와 README는 새 수신시각보다 이전 loop
   시각으로 만료를 검사해 false offline을 만든 문제를 기록한다. monotonic 차분은
   동일 clock 영역의 현재 값으로 계산하고 rollover test를 둔다.
4. context v2.2.4는 BLE deinit 시 host callback과 server/advertising 객체 삭제의
   race를 기록한다. 객체를 재생성/파괴하는 회복 loop를 만들지 않는다.
5. context의 heap/LoadProhibited 관련 기록은 networking/decoder/callback 수명과
   메모리 한계의 위험을 보여준다. 모든 과거 crash의 단일 원인이 증명됐다고
   단정하지 않는다. bounded buffers, 명시적 소유권, 작업 세대 검사를 사용한다.
6. BLE 광고 시작/보안/AMS 구독은 서로 다른 상태다. 초기화 성공을 media ready로
   간주하지 않고 timeout/재시도/실제 connection 상태를 표시한다.
7. OTA checking stuck과 동기 영상 늦은 control 재전송을 피하기 위해 자동
   무한 검사 없이 수동 OTA 창, 시간 기준 frame 선택, 최신 요청 mailbox를 사용한다.

## 유지하지 않는 것

MAIN/ZERO inter-board SPI, 오래된 GPIO, shared TFT/SD SPI, 재부팅을 통한 profile
선택, SAFE bundle/legacy signing keys, `.inc` 조각 공유, 펌웨어에서 직접 수행하는 artwork HTML 검색,
실물 시험으로 검증되지 않은 성능 수치, legacy build/release 스크립트 의존성.

## 신규 기능

AHT10, 1-cell battery ADC filtering/LUT, 5개 SK6812 효과, RED/GREEN PWM manager,
GPIO5 전원 UX, line-synced LRC parser/timeline/renderer, metadata helper protocol,
240×320 partial transfer, host logic tests를 새로 구현한다.
