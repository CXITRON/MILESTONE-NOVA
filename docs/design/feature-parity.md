# U0001 기능 대조 및 검증 근거

2026-09-27. 기존 `MILESTONE_Legacy/MILESTONE_Core/V5_PARITY_CHECKLIST.md`,
`v5/IMPLEMENTATION_STATUS.md`, 실제 CORE/포털/Artwork/Video/Sync/설정 소스를 대조했다.
C0001의 완료 판정은 철회 상태로 유지한다. 이 표의 **구현됨**은 아래 NOVA 코드에
사용 흐름이 연결되었다는 뜻이며, 실물 동작을 확인했다는 뜻이 아니다.
모든 실물 항목은 [hardware-validation](../guides/hardware-validation.md)에 별도로 남긴다.

2026-09-27 추가: Sync 파일 전체 CRC 대조와 재연결/종료 순서, 아트·가사 변경 감지,
보호 상태 재검사/오래된 결과 폐기, 재조회 실패 시 원본 유지, 업로드 취소·저장 실패를
회귀 검사에 추가했다. 종료 시에는 AP 닫힘과 별도로 service worker의 I/O 종료·로그
drain을 기다린다. 자세한 변경과 09-29 빌드 입력 대조 결과는
[C0003](../agents/reports/report_C0003_2026_09_29.md)에 기록한다.

2026-10-03 추가: 사용자 추가 지시로 NOW 통합 레이아웃(5), 172px 아트와 텍스트 두 줄,
Worker 경유 온라인 동기 가사를 연결했다. Claude Code의 초기 구현과 Codex의 서버 완성·
보완·통합 검증 범위는 [C0008](../agents/reports/report_C0008_2026_10_03.md)에 기록한다.

2026-10-05 추가: 일본어 글꼴, 저장된 부팅 문구, 같은 곡의 아트/가사 재읽기,
화면 지연 중 버튼 취득을 수정해 연결 기기에 업로드했다. 수정·호스트 검사와 실기 로그의
범위는 [C0010](../agents/reports/report_C0010_2026_10_05.md)에 구분한다. Worker 배포는
[C0009](../agents/reports/report_C0009_2026_10_03.md)에서 완료되었으며 이번에는 변경하지 않았다.

기존 코드 표의 경로는 `MILESTONE_Legacy/MILESTONE_Core/` 기준이다.
`core` 검사는 tests/test_core.cpp, `parity`는 tests/test_parity.cpp,
`storage`는 실제 StorageFiles/MediaDecoder/Journal을 실행하는 tests/storage/test_storage.cpp,
`browser`는 tests/portal/browser.html을 뜻한다.

| 기능 / 보존한 사용 흐름 | 기존 근거 | NOVA 연결 코드 | 검증 / 상태 |
|---|---|---|---|
| MENU → CORE/MEDIA/NOW, OK 적용/BACK 취소, 재부팅 없는 전환 | V5_PARITY_CHECKLIST checkpoint 1, v5 Main 입력 | src/ui/Navigation.*, src/app/App.cpp::input/selectMode, src/input/Buttons.*·InputEvents.* | 구현됨. parity 메뉴/순서/mask, 5ms 독립 입력 취득과 큐의 지연·동시 입력·포화 검사. 물리 버튼 조작 미검증 |
| 기존 CORE 7개 조합, Focus/환경 추가, 3초 splash | v5/.../V5CoreViews.h, CoreDisplay.inc | src/ui/CoreScreens.cpp, Ui.cpp, App.cpp | 구현됨. 날짜/focus core 검사, 저장한 문구·색상·빈 문구를 부팅 화면에 적용하는 실제 renderer 검사 |
| 색/12·24시간/초/이름/문구/정렬·스크롤/순서·mask/자동 순환/번인/화면 끄기 | V5CoreViews.h, V5Portal.h, legacy settings | src/settings/Values.*, src/ui/*, src/display/Canvas.*, AppServices.cpp | 구현됨. 범위/UTF-8 검사, 실제 renderer. 아트 픽셀과 UI 색 설정을 분리 |
| NOW 6개 레이아웃(아트+가사 포함), player/queue/긴 metadata, iPhone 지원 명령 | CoreBluetooth.inc, V5AmsRuntime.inc, MilestoneV5Now.cpp 및 10-03 추가 지시 | src/media/BleMedia.*, AmsDecoder.*, Session.*, src/ui/NowScreen.cpp | 구현됨. 일본어 가사·혼합 글꼴 포함 총 67장 UI 렌더. 실기 AMS 메타데이터·곡 전환 로그 확인. 지원 목록 없으면 제어 거절; 실제 재생 제어/패널 육안 확인 미검증 |
| 로컬 LRC 우선, Worker 온라인 동기 가사와 SD 캐시 | U0001 LRC 및 10-03 추가 지시 | src/lyrics/OnlineLyrics.*, src/media/TrackAssets.*, src/storage/StorageFiles.cpp, MILESTONE_Core/services/artwork-worker/src/lyrics.js | 구현됨. 오류/크기/로컬 파일 보호/늦은 결과 검사. Worker 배포 완료(C0009). 같은 곡 재읽기 중 표시 유지·중복 조회 방지 검사, 실기 SD 가사 읽기 확인. LCD 동기화/장시간 장애 미검증 |
| 설정 AP, 임의 8자리/고정/확인한 open, 화면에만 암호, captive portal/BACK 종료 | V5Portal.h, V5Radio.h | src/network/Network.*, src/portal/*, assets/portal/*, App.cpp | 구현됨. ESP32 build, offline 초기 DOM. 실제 AP/WebServer 조작 미검증 |
| 비동기 검색/Personal·Open·PEAP/15초 시험/2초 성공 후 저장/최대 8개/재사용·삭제 | V5Network.h, MilestoneV5Legacy.h | Network.*, settings/Values.*·Store.*, AppServices.cpp | 구현됨. 프로필 validation/성공 순서/상한 host 검사. RF/NVS 실물 미검증 |
| NTP/브라우저 시간/UTC RTC/TZ/기본값/초기화/schema 1~12 import | V5Rtc, V5Portal, MilestoneV5Legacy.h | sensors/Sensors.*, settings/Store.*·LegacyImport.*, AppServices.cpp | 구현됨. 날짜/TZ/범위 core 검사. namespace 실제 import/전원 차단 미검증 |
| 로컬 BMP/MVJ1/MSM1 및 NVI1/NVV1/NJV1, 이전·다음·pause/반복/자동 전환·정렬·흑백 | CoreMedia.*, V5Video.h, MilestoneV5Video.* | storage/MediaDecoder.*, StorageFiles.cpp, media/Formats.*·Playback.*, App.cpp | 구현됨. storage의 JPEG/MSM seek·BMP 방향·CRC/취소, palette/흑백 renderer 검사 |
| 브라우저 사진/GIF/영상 변환/미리보기/SD 저장/목록·이름·순서·사용·삭제·clear·repair | V5Portal.h | assets/portal/convert.js·gif.js·transfer.js·app.js, Portal.cpp, StorageFiles.cpp | 구현됨. 실제 browser PNG/MP4/GIF codec, IndexedDB; storage 교체/실패 주입 |
| AP Sync SD 영상 + 브라우저 원본 오디오, chunk 재개·검증·pause·seek·timeout·BACK 정리 | V5SyncMedia.h, V5Video.h, WORK_CHECKPOINT_20260913_HOTFIX.md | Playback.*, App.cpp·AppServices.cpp, Portal.cpp, assets/portal/transfer.js·app.js | 구현됨. session/sequence/rollover/stale, 늦은 tick/stop 순서, chunk 재개·CRC. 목록 갱신 시 Sync 유지, 모드 이탈 시 파일 정리, SD 작업 중에도 timeout 검사. 음영상 오차는 실물 미측정 |
| 기존 Artwork Worker 자동 조회/MAC1 검증/곡별 캐시/원자 교체·복구 | services/artwork-worker, V5Artwork.h | artwork/Artwork.*, network/Http.*·Endpoints.h, Storage.cpp·StorageFiles.cpp | 구현됨. 실제 Worker HTTP 200 및 22704-byte MAC1 CRC/치수 확인. ESP TLS/RF 미검증 |
| 아트 검색/미리보기/사용자 업로드·교체/고정·보호 해제/차단/삭제 MISSING/재조회/LRU | V5ArtworkPortal.h | Portal.cpp::artwork/chunk, StorageFiles.cpp의 Art*, assets/portal/app.js | 구현됨. metadata 검색·32개 pagination·보호·삭제·재조회·quota host 검사 |
| 환경 C/F·보정·주기·표시/경고·위험·재검색/날짜별 CSV/중단 기록 복구 | V5Environment.h, V5Portal.h | sensors/Sensors.*, Values.*, App.cpp, storage/Journal.*·StorageFiles.cpp | 구현됨. 입력 검사, 완료/부분 append 복구·실패 주입. AHT10 실물 미검증 |
| LED 주야간 밝기/효과/알림, 내부 온도 감속·중지·복귀 | V5 LED/thermal code, U0001 신규 배선 | lighting/Lights.*, core/Logic.cpp::thermalLevel, App.cpp | 구현됨. 5°C hysteresis/비정상 측정 host 검사. Sync/버튼 재시작 차단. 실물 온도/LED 미검증 |
| 상세 정보 12페이지, reset/heap/stack/loop/SD/무선/열, CRC 진단 이력 export/clear | CoreDiagnostics.*, MilestoneV5Diagnostics.*, v5 MAIN | logging/Diagnostics.*, AppServices.cpp::publish, CoreScreens.cpp | 구현됨. 빌드·화면 렌더. NVS 이력/장시간 실물 미검증 |
| 인터넷 확인(부팅·하루 1회·수동)/다운로드/SD signed 후보/물리 확인/A/B rollback/복구 메뉴 | CoreUpdate.inc, V5BundleUpdate.h | update/Firmware.*·AutoUpdate.*·BootConfirm.*·Descriptor.cpp, scripts/build/release.py | 구현됨. 암호·키 설정 없이 내장 공개키로 검증. 실제 build descriptor, RSA 서명·변조 거절, 부팅/하루 1회/실패 1시간 재시도 일정, 부팅 확정 host 검사. 기기 무선 설치·실물 rollback은 릴리스 게시 후 시험 |
| OFF 저장·worker 종료·SD close·LED/LCD OFF, GPIO5 wake/중복 입력 억제 | U0001 신규 전원 UX, 기존 전원 흐름 | power/Power.*, App.cpp::shutdown, input/Button.*·Buttons.* | 구현됨. 버튼/rollover logic 검사. 실물 wake·전류 미측정 |

## 범위 해석과 운영 조건

- 단일보드에 없는 MAIN/ZERO 통신·복제·다중 이미지/SAFE 설치, 과거 GPIO, 폐기된 live
  frame streaming을 넣지 않았다. 그 구조가 제공하던 모드/미디어/설정/업데이트 사용자
  기능은 위 모듈로 재구현했다. legacy 파일/키/빌드 스크립트에 의존하지 않는다.
- AMS는 아트/LRC를 전송하지 않는다. 아트는 기존 Worker 또는 사용자 업로드,
  가사는 local LRC 우선이며 10-03 추가 지시로 Worker 경유 LRCLIB 조회를 구현했다.
  서버 배포는 C0009에서 확인했고, 실물 LCD 동기화와 장시간 동작은 별도다.
  native companion player는 여전히 향후 확장 경로다.
- helper metadata 연결은 지원 명령을 증명하지 않으므로 AMS 제어 UI를 활성화하지 않는다.
  실제 GATT write 응답도 iPhone 플레이어 상태 변경을 대신하지 않는다.
- 공개 NOVA 릴리스 파일은 이번 작업에서 게시하지 않았다. 인터넷 업데이트 운영에는
  사용자의 NOVA 서명 키로 만든 독립 manifest/bin 게시가 필요하다. 준비 도구와
  SD signed 후보 경로는 제공한다. legacy release 카탈로그로 대체하지 않는다.
- Factory reset은 NOVA NVS를 초기화한다. SD 파일은 남으므로 재프로비저닝을 원하지
  않으면 SD의 config/device.ini, secrets.ini를 제거해야 한다.
- 빌드 성공, 파일 형식/로직/브라우저 검사와 실제 기기의 장시간 동작을 구분한다.
  물리 기기에서 검증하기 전까지 "누락 없이 실제로 모두 동작한다"고 단정하지 않는다.
