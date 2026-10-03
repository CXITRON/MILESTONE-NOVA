# 실물 검증 체크리스트

U0001 · Hardware Validation: **Not performed** · Hardware Validation Required: **Yes**

컴파일과 호스트 검사만으로 아래 항목을 통과한 것으로 보지 않는다. 로그의 reset reason,
heap/min heap, 최대 loop 간격과 실패 시점을 함께 기록한다. 측정 장비/보드 revision/
SD 모델/OS 버전/펌웨어 hash를 시험 기록에 남긴다.

| 항목 | 절차 및 합격 기준 | 상태 |
|---|---|---|
| 보드 | LOLIN S3 Pro 16MB/8MB OPI 확인, 부팅 PSRAM 값 확인 | 미실시 |
| NOW 아트 | Worker v3 갱신 배포 후 200×200 원본을 큰 레이아웃 172×172·작은 레이아웃 88×88로 표시, 배포 전 MAC1·이전 160 캐시 표시, 진행 바/제목/아티스트 겹침 없음 | host 디코딩·대체 경로·렌더 검사 통과, 실물/배포 후 실서비스 미실시 |
| MEDIA 240 | 240×240 사진/GIF/영상 표시, 영상 실제 FPS(20 MHz/상향 후), 이전 160 파일 확대, 48 KiB JPEG 프레임, 흑백 | host 디코딩·브라우저 변환 통과, 실물 미실시 |
| 5V_AUX | LOLIN 장착 전 DM13B·74HCT125N 납땜, 극성/단락 확인, USB만 연결해 74HCT125N 14–7번 5 V 측정, 배터리만 연결 시에도 5 V 유지 | 미실시 |
| GPIO | README와 배선 대조, 예비/USB/UART/flash 핀 충돌 없음 | 코드 검사 통과, 배선 미실시 |
| strap | 배터리 divider 및 GPIO46 SD CS가 reset/boot 조건을 방해하지 않는지 확인 | 미실시 |
| LCD | 20MHz 시작, 240×320 방향/색/inversion/테두리/10cm 배선 확인 | 미실시 |
| redraw | NOW 가사/시계/영상에서 깜빡임, tearing, 버튼 반응 확인 | host layout 확인, 패널 미실시 |
| 버튼 | bounce, 동시 입력, short/long/repeat, 깨우는 OK 중복 입력 없음 | logic 통과, 실물 미실시 |
| I2C | DS3231만/AHT10만/둘 다/없음/SDA stuck 상태에서 reboot loop 없음 | 미실시 |
| RTC | OSF, 전지 분리, 12/24h, 날짜 경계, UTC 저장/TZ 표시, NTP 복구 | 미실시 |
| AHT10 | 온습도 기준계 비교, sensor 제거/재연결, stale 표시 | 미실시 |
| 배터리 | JP2 `BAT_AD` 연결 확인(외부 divider 없음), 멀티미터와 ADC 비교, gain/offset 조정, 3.55/3.25V 경고 hysteresis, 전원 없는 ADC | logic LUT 통과, 실물 미실시 |
| SD | 정상/없음/mount failure/손상파일/read 중 제거; UI가 살아 있고 타 파일 오염 없음 | 파일 포맷 host 통과, 실물 미실시 |
| assets | 한글/영문 LRC, offset/seek/pause/중복/empty/unsynced/누락/큰 파일 | host 통과, 실물 미실시 |
| AMS | iPhone 최초 pairing, bond 재연결, AMS 늦은 게시, 앱/곡 변경, title/artist/album 누락 | 미실시 |
| helper | 기본 MTU, UTF-8 분할, 끊긴 transaction, pause/seek, 새 세션, queue overflow | parser/packer 통과, 무선 미실시 |
| Wi-Fi | AP 없음/암호 오류/재연결/NTP timeout, BLE 동시 사용 시 연속성 | 미실시 |
| RGB | SK6812 3535 GRB 순서, 5개 chain, brightness 상한(96/255), 74HCT125N 4채널 출력 5 V, 미사용 채널 고정, sleep OFF | 미실시 |
| 상태 LED | 정상 RED OFF, GREEN 4초 breathing, SD/저전압/OTA 실패 | 미실시 |
| OFF | settings 저장, worker 반환/파일 close 후 SD 종료, 모든 LED/backlight OFF | 미실시 |
| wake | GPIO5 LOW만 wake, 나머지 4버튼은 wake하지 않음; hold OK 때 즉시 재수면 loop 없음 | 미실시 |
| 전류 | USB/배터리 각각 idle/화면/무선/OFF 전류. OFF 중 DM13B·SK6812·74HCT125N 대기 전류(5V_AUX 미차단)와 예상 대기 시간 기록 | 미실시 |
| signed OTA | 올바른 키/틀린 키/unsigned/손상/초과 크기/timeout, 기존 slot 보존 | compile 및 host 서명/변조 거절 통과, 실물 미실시 |
| rollback | 검증 전 reset → 이전 이미지, 60초 runtime 후 확정, 전송/flash 중 전원 차단 | SDK 설정 확인, 실물 미실시 |
| NVS | 잘못된 schema/CRC/범위, 저장 실패, 설정 변경/전원 차단 후 재부팅 | validation host 통과, 실물 미실시 |
| soak | 24~72h 재생/곡 변경/가사/SD/Wi-Fi 혼합; heap 감소 추세, stack/loop/panic 확인 | 미실시 |

## 재작업으로 추가된 실물 시험

| 경로 | 확인할 내용 | 현재 증거 |
|---|---|---|
| 모드/AP | MENU → 모드 선택/취소, 재부팅 없음, NOW만 BLE, AP BACK 종료/재개 | Navigation host 검사 + 36장 실제 C++ 렌더 |
| 포털 | 실제 기기에서 모든 탭/한국어/모바일 입력, AP token 재발행, 다른 인터페이스 요청 거절 | offline 자산 compile, 로컬 fixture의 초기 DOM 확인; 전체 기기 UI 조작 미실시 |
| Wi-Fi | Open/Personal/PEAP, 오류 암호/15초 실패/2초 성공, 8개 순서·삭제·A/B NVS, AP 유지 | 프로필 순서/validation host 검사; RF 및 NVS 실패 주입 미실시 |
| 변환/재개 | iPhone Safari/Android/PC에서 사진/GIF/MP4, 탭 reload/전원 차단 후 offset 재개 | 실제 Chromium codec/IndexedDB, Node chunk CRC, 실제 Storage handler 실패 주입 통과 |
| 미디어 | MVJ1/NJV1/MSM1/BMP/NVI1/NVV1, pause/seek/자동 순환/흑백, 64개/초과 목록, SD 제거·재삽입·repair | decoder/CRC/index host 검사. JPEG는 host libjpeg adapter이며 ESP decoder/SPI는 미검증 |
| AP Sync | 준비 → 원본 소리/SD 영상, pause/seek/끝, 새 session/sequence, 지연·역순·끊김·BACK, thermal stop | Playback/SyncClock 시간 순서 검사; 실제 음영상 오차·RF 공존 미측정 |
| Artwork | iPhone 곡 변경 → Worker → MAC1 → 캐시, 긴 문자열/한글, pin/custom/block/MISSING/LRU/여유 공간 | 실서비스 HTTP 200, 22704 bytes/dimensions/CRC 확인; SD 관리 host 검사 |
| AMS 확장 | 긴 Entity Attribute read, player/queue, 지원 명령 갱신, GATT write 실패/재구독 | parser/상태 코드 compile; 실제 iOS 액세서리 연결/명령 수행 미검증 |
| 로그 | 날짜별 CSV, 기록 중 전원 차단, 전체/부분 append 복구, 손상 pending 보존 | 실제 journal/file handler로 완료·부분 write 복구 검사 |
| 진단 | reset/loop/heap/stack/열/저장소 상태, CRC A/B 이력, export/clear | 코드 compile; 장시간/실제 reset 원인 이력은 미검증 |
| 설정 | 전원 차단 A/B 선택, schema 1..12 import 원본 보존, 기본값/초기화/SD 재프로비저닝 | 범위/입력 host 검사; 실제 Preferences/NVS power cut는 미검증 |
| 업데이트 | 공개 manifest/다운로드/SD 후보 → 물리 확인 → A/B, 잘못된 제품/키/hash, 설치 직전 후보 변경 | 실제 NOVA image descriptor + RSA 2048/4096 서명/변조/manifest 검사; flash/boot 미검증 |
| 열 | warn/throttle/stop 각 5°C 복귀 hysteresis, 잘못된 온도, 재생/Sync/업데이트 제어 제한 | pure thermal logic 검사; 실제 칩 온도/80MHz·RF 영향 미측정 |

실제 iPhone/LOLIN S3 Pro/AP 환경에 접근하지 않고 통과 처리하지 않는다. 로컬 브라우저
컴포넌트 검사는 2026-09-24 Chromium에서 PNG→NVI, MP4/GIF→NJV(모든 프레임 CRC),
시간에 따른 프레임 변경, GIF 지연, IndexedDB 재연결, 취소를 통과했다. 포털 상호작용
자동 검사는 브라우저 제어 도구의 입력/화면 캡처 timeout으로 완료하지 못했으며, 초기 DOM과
codec 결과만 확인했다. 서비스 worker의 실제 WebServer 처리와 GPIO 조작을 대체하지 않는다.


LCD 속도 상향, 최대 video FPS, 배터리 % 정확도와 장시간 안정성은 이 결과가
있기 전에는 제품의 보장 수치로 사용하지 않는다. fatal한 전기 문제는 software
degraded mode로 해결할 수 없으므로 전원/레벨/배선은 별도로 검증한다.

## 2026-09-27 추가 회귀 및 실물 확인 항목

| 경로 | 이번 검사 | 실물에서 남은 확인 |
|---|---|---|
| Sync 원본 일치 | 실제 decoder/SD handler로 전체 CRC·크기·NJV1 제한, 다른 정상 영상/바뀐 FPS 거절 | 업로드 실패 뒤 연결 거절, 재연결 후 실제 음영상 오차 |
| Sync 연결 수명 | Node에서 느린 start 뒤 연속 start/stop, 진행 중 tick, 세션별 stop 순서 통과 | 여러 탭과 AP BACK/모드 전환, 종료 후 재연결 |
| 포털 저장 실패 | Node에서 IndexedDB request+transaction 이중 실패와 전체 CRC writer 검사 | 저장소 금지 브라우저에서도 설정/연결/진단 UI 사용 가능 여부 |
| 업로드 취소 | 취소 전 begin 차단, 진행 중 begin에 abort 전달, 잘못된 offset·빈 chunk 거절 | 실제 브라우저 취소/새로고침 및 SD checkpoint |
| 아트/LRC 갱신 | revision 성공/실패, 실제 Artwork 모듈의 오래된 결과 폐기와 보호 상태 재검사 | 현재 곡의 아트/LRC 저장 직후 반영, AP 전후 대기 요청 |
| 아트 요청/재조회 | 1400-byte body 경계, 늦은 자동 저장 거절, 기존 이미지 유지, 느린 body deadline 검사 | TLS 오류/느린 서버/SD 부족 시 화면·버튼 반응 |
| 설정 ACK | 최신 snapshot 게시 후 ACK하도록 연결, ESP32 빌드 통과 | 빠른 연속 설정 저장/설정 변경 중 Sync 화면 유지 |
| OFF | service I/O 종료·로그 drain ACK 뒤 SD stop, 실패 취소 시 재개하도록 연결 | HTTPS/로그 처리 중 OFF, NVS 실패 후 AP 재사용, 실제 전류 |

이번 Artwork host 검사는 HTTP transport와 시계를 대체한 것이며 실서비스/TLS/RTOS
검사가 아니다. 2026-09-27 및 09-29에는 브라우저 도구의 사용 가능한 브라우저가 0개여서 변경 후
실제 포털/codec 검사를 재실행하지 못했다. 09-24 브라우저 검사 결과를 이번 변경의
재검증 결과로 계산하지 않는다. Hardware Validation은 계속 Not performed다.

## 2026-10-03 온라인 가사·NOW 추가 확인 항목

[공동 작업 보고서 C0008](../codex/reports/report_C0008_2026_10_03.md)의 구현과 호스트 검사를
기준으로 한다. 실제 LRCLIB 응답은 로컬 Worker에서 확인했으며 공개 Worker는 아직 배포하지 않았다.

| 경로 | 이번 검사 | 실물에서 남은 확인 |
|---|---|---|
| NOW 레이아웃 | 0..5 설정 범위, 통합 화면 5·가사 없음·비동기 가사를 포함한 실제 C++ 화면 63장 렌더 | 기기·포털에서 6종 선택과 재부팅 후 유지, 172px 아트/두 줄 정보/88px 통합 화면의 가독성 |
| 긴 음악 정보 | 제목·아티스트·앨범에 기존 marquee 연결, 고정 시각의 렌더 검사 | `scroll` ON/OFF와 속도, 한글/영문 혼합 긴 문자열의 양 끝 2초 정지·왕복, 새 곡 전환. 큰 글씨 레이아웃과 가사는 wrap/clip 경로도 확인 |
| 온라인 가사 | Worker 11개 검사, 실제 LRCLIB → 로컬 Worker 200 LRC 검증·캐시 재사용 | 배포한 `/v1/lyrics`를 ESP TLS로 조회, 실제 AMS 제목/아티스트/앨범/재생 길이 조합, pause/seek/곡 변경에 따른 가사 동기화 |
| 로컬 우선 | 기존·빈 LRC 우선, 조회 도중 도착한 로컬 파일 보호, 오래된 세대 결과 폐기 | 다운로드 후 SD 재사용/재부팅, 포털 LRC 업로드와 곡 변경, 파일 교체 직후 화면 갱신 |
| 장애와 RAM | SD 없음·저장 실패 시 RAM 표시, HTTP 오류/잘못된 UTF-8·LRC/초과 크기/미완료·느린 body 거절 | SD 제거·가득 참, TLS 실패·약한 Wi-Fi·서버 204/503/504에서도 버튼·NOW 유지. `lyrics_view` OFF 시 새 조회 중단 및 `artwork_auto`와 독립 여부 |
| 서비스 종료 | 앞선 아트 요청 뒤 AP/OFF 요청을 재확인하도록 연결, 빌드·host 검사 통과 | 아트/가사 HTTPS 및 SD 저장 중 AP 진입/OFF. 진행 중 transport timeout을 포함한 실제 종료 시간과 watchdog/stack 측정 |

이번에는 기기 업로드와 브라우저 포털 상호작용 검사를 수행하지 않았다. 포털 JavaScript
구문 및 기존 Node 회귀 검사는 통과했다. 63장 렌더는 ESP32 LCD/RTOS/RF 성능 검증을
대체하지 않는다. Hardware Validation은 계속 **Not performed**, Required는 **Yes**다.
