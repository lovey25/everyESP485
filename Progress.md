# everyESP485 고도화 작업 계획

ESP8266 RS485↔WebSocket 브리지의 사용 편의성, 성능, 디자인 개선 로드맵.

우선순위:

- **P1** — 즉효성 높음 (1~2일 내 적용 가능, 임팩트 큼)
- **P2** — 중기 (구조 개선 동반, 1주 단위)
- **P3** — 장기/선택 (보안·CI·실험 항목)

진행 표기:

- [ ] 미착수
- [~] 진행 중
- [x] 완료

---

## 1. 사용 편의성 (UX)

### 1.1 모니터 페이지 ([data/monitor.htm](data/monitor.htm))

- [x] **(P1) 타임스탬프 컬럼** — `addLine`에 ms 단위 prefix. 패킷 간격 분석.
- [x] **(P1) HEX/ASCII 동시 표시** — xxd 스타일 `오프셋 | HEX(16) | ASCII` 한 줄 포맷. 64바이트 truncation 제거.
- [x] **(P1) RX/TX 방향 색상 구분** — 수신(`<<`)/송신(`>>`)/sys prefix + 색 분리.
- [x] **(P1) 컨트롤 버튼 추가** — 일시정지 / 클리어 / 로그 다운로드(.log).
- [x] **(P1) WS 자동 재접속** — `onclose` 시 지수 백오프(1→2→5→10→30s) + 상태 뱃지(connecting/connected/reconnecting).
- [ ] **(P2) DOM 가상 스크롤** — `<div>` 폭주 방지(>100pps 끊김), `requestAnimationFrame` 일괄 flush.

### 1.2 프리셋

- [x] **(P1) 스키마 확장** — `{version:1, data:[{name, hex, note}, ...]}`. 구 형식(string array)도 자동 변환·마이그레이션.
- [x] **(P1) CRC16(Modbus) 자동 계산** — `+CRC` 토글 (HEX 모드에서만 활성, ASCII에선 disabled). 입력란 송신 경로에만 적용, preset 호출(`callPreset`)은 `skipCrc:true`로 우회.
- [x] **(P1) 인라인 편집** — popup 내 행 단위 name/hex 입력 + 저장 버튼.
- [ ] **(P2) 반복 전송 옵션** — `repeat`/`intervalMs` 주기 송신(Modbus 폴링용).

### 1.3 디바이스 정보 / 진단

- [x] **(P1) `/api/status` 엔드포인트** — 펌웨어 버전, 빌드 시각, IP, RSSI, free heap, heap frag, uptime, WS 클라 수, 누적/최근 통계.
- [x] **(P1) 모니터 헤더 표시** — 2초 폴링, `v버전 · IP · RSSI · heap · uptime · ws클라이언트 · drops`.

### 1.4 WiFi 설정 ([data/wifi.htm](data/wifi.htm))

- [x] **(P2) 비밀번호 마스킹** — `type="password"` + 표시/숨김 토글 버튼 ([data/wifi.htm](data/wifi.htm)).
- [x] **(P2) 신호세기/암호화 아이콘** — RSSI → 4단 막대 시각화, 🔒/🔓 잠금 아이콘, 신호세기 내림차순 정렬.
- [ ] **(P2) 연결 진행 상태 푸시** — SSE/WS로 "saving → restarting → reconnecting → IP=…" 표시. 현재는 [src/web_routes.cpp](src/web_routes.cpp) `/connect2ssid`가 200 응답 후 즉시 재부팅. (toast로 "저장됨/재부팅" 안내는 추가, 실제 진행 상태 푸시는 미구현)

### 1.5 모바일/접근성

- [x] **(P2) 가상 키보드 영역 보정** — `safe-area-inset-top/bottom` + `100dvh` 적용 ([data/style.css](data/style.css)).
- [ ] **(P2) 다크/라이트 토글, 폰트 크기 조절** — 다크 단일 테마로 통일(현재 시점). 라이트 토글은 별도 스프린트에서.
- [x] **(P2) 키보드 단축키** — `/`(검색 토글), Esc(검색 닫기 / 외부에서는 클리어), Space(일시정지) ([data/monitor.htm](data/monitor.htm) `setupShortcuts`).

---

## 2. 성능 (Performance)

### 2.1 펌웨어 코어 ([src/main.cpp](src/main.cpp))

- [x] **(P1) `delay(packTimeout)` 제거** — `micros() - lastByteUs >= PACK_TIMEOUT_US` 비차단 idle 검사로 교체. inner-while 제거.
- [x] **(P1) WS fragment 버퍼링** — `wsRxBuf[8KB]`에 누적 후 `info->final && index+len == info->len`에서만 일괄 `Serial.write`.
- [x] **(P1) WS backpressure 가드** — `ws.count() == 0` skip + `ESP.getFreeHeap() < WS_HEAP_GUARD` 시 drop. `wsBinDropped`/`wsBinDroppedTotal`로 카운트.
- [ ] **(P2) 링버퍼 도입** — 8KB 단일 `buf` ([src/main.cpp:378](src/main.cpp#L378))를 16KB 링버퍼로 교체. 송신 중에도 수신 계속.

### 2.2 UART/RS485

- [ ] **(P2) Serial1으로 디버그 분리** — GPIO2 TX-only Serial1(또는 SoftwareSerial)로 디버그 출력 → `RS485_CLEAN_BUS` 매크로 자체를 제거. RS485 라인 항상 깨끗 + 디버깅 편의 회복.
- [ ] **(P2) DE/RE 핀 명시 제어** — 자동방향 모듈(XY-017) 대신 GPIO 제어 → 고속 baud 안정.
- [ ] **(P2) 가변 baud / 파리티 옵션** — `/api/config`로 9600/19200/.../115200, 8N1/8E1/8O1 선택, `/baud.ini` 영구화.

### 2.3 빌드/메모리

- [ ] **(P3) SPIFFS → LittleFS** — 마운트/쓰기 속도 개선, ESP8266 코어 권장.
- [ ] **(P3) gzip 정적 압축** — `data/*.htm`, `*.css`를 `.gz`로 저장, `serveStatic` 자동 처리.
- [ ] **(P3) `-O2` 빌드 비교** — heap/플래시 영향 측정.

---

## 3. 디자인 (Architecture / UI)

### 3.1 펌웨어 구조 분리

[src/main.cpp](src/main.cpp) 단일 파일을 다음으로 분할 (실제 적용된 구조):

- [x] **(P1) `src/dbg.h`** — `RS485_CLEAN_BUS` + `DBG_PRINT*` 매크로.
- [x] **(P1) `src/version.h`** — `EVERYESP_VERSION`, `EVERYESP_BUILD`, `hostName extern`.
- [x] **(P1) `src/wifi_manager.{h,cpp}`** — `setupConnection`, AP/STA 전환, `/config.ini` IO.
- [x] **(P1) `src/bridge.{h,cpp}`** — `ws` 객체 소유, 카운터, `webSocketEvent`, `bridgeSetup/Loop`.
- [x] **(P1) `src/web_routes.{h,cpp}`** — `server`/`events` 객체 소유, `/api/status`·`/scan`·`/wifistatus`·`/wifireset`·`/connect2ssid`·`/savepreset` 라우트, `SPIFFSEditor` 핸들러. (preset 저장은 별도 모듈로 분리할 만큼 로직이 없어 통합)
- [x] **`src/main.cpp`** — `setup()`/`loop()` 골격, `setupOTA`, `hostName` 정의.

### 3.2 UI 스타일 통일

- [x] **(P1) `style.css` 단일화** — index/wifi의 인라인 `<style>` 제거 후 `body class="page-home"`/`page-wifi`로 분기. 색상은 `:root` 변수(`--bg`, `--fg`, `--mono` 등)로 토큰화.
- [x] **(P1) 폰트 fallback** — `--mono: monaco, ui-monospace, "SF Mono", Menlo, Consolas, monospace`.
- [x] **(P2) 버튼/입력 디자인 토큰화** — `:root`에 surface/foreground/accent/spacing/radius 토큰 도입, 공용 `button`/`input` 베이스 + `.btn-primary/.btn-danger/.btn-ghost` variant ([data/style.css](data/style.css)).
- [x] **디자인 현대화** — index 카드 레이아웃 + 그라디언트 로고, wifi 섹션화된 폼 + 신호 막대/잠금/토스트, monitor 모던 토픽바·검색 바·preset 팝업 헤더 적용. dark surface 토큰으로 페이지 간 톤 통일.

### 3.3 API 설계

엔드포인트 정리:

- [ ] **(P2) `GET  /api/status`**
- [ ] **(P2) `GET  /api/wifi/scan`, `POST /api/wifi/connect`, `POST /api/wifi/reset`**
- [ ] **(P2) `GET/PUT /api/preset`**
- [ ] **(P2) `GET/PUT /api/config`** — baud, parity, hostname.
- [x] **(P2) ArduinoJson 도입** — `/connect2ssid` + `/savepreset` chunked body 누적 후 `JsonDocument` + `deserializeJson`. 크기 가드(1KB/4KB) + 400/413/500 응답.
- [ ] **(P2) `/wifireset` GET → POST + 확인** — 현재 [src/main.cpp:238](src/main.cpp#L238)이 GET이라 브라우저 prefetch/봇이 트리거 가능.

### 3.4 보안

- [ ] **(P3) Basic auth 적용** — `/ws`, `/monitor.htm`, `/wifireset`, `/connect2ssid`, `/savepreset` 무인증 상태. `AsyncAuthenticationMiddleware`로 mutating 엔드포인트 보호.
- [ ] **(P3) OTA 비밀번호 이관** — [src/main.cpp:314](src/main.cpp#L314) `admin` 하드코딩 → `credentials.h`.
- [ ] **(P3) SPIFFSEditor 접근 차단 검증** — `/config.ini` 평문이 Editor로 노출되지 않게.

---

## 4. 운영 / QA

- [x] **(P1) README/test/CLAUDE.md 정합성 수정** — `BAUD = 9600`로 통일. README "현재 설정" 섹션과 CLAUDE.md "Architecture/Constraints" 섹션을 신규 동작(non-blocking 루프, fragment 재조립, heap-guard backpressure, ArduinoJson, /api/status, preset v1 스키마)에 맞게 갱신.
- [ ] **(P2) 호스트 사이드 단위 테스트** — `pio test` (preset 파서, CRC, 링버퍼).
- [ ] **(P3) GitHub Actions CI** — `pio run` 빌드 검증.
- [ ] **(P3) `version.h` 자동 주입** — git short hash + 빌드 타임 (PIO `extra_script`).

---

## 추천 1차 스프린트 (1~2일)

성능·안정성·UX 임팩트가 가장 큰 P1 묶음. 회귀 위험이 있으므로 코어 3개는 함께 처리:

1. **펌웨어 코어 안정화 묶음**
   - `delay(packTimeout)` 제거
   - WS fragment 버퍼링
   - WS backpressure 가드
2. **모니터 UX 개선**
   - 타임스탬프
   - RX/TX 색구분
   - 일시정지 / 클리어 / 로그 저장
   - 자동 재접속
3. **프리셋 스키마 확장 + ArduinoJson 도입**
4. **`/api/status` + 디바이스 정보 헤더**
5. **README / test_serial.py baud 정합성 수정**

---

## 변경 이력

- 2026-05-09: 초기 계획 작성.
- 2026-05-09: 1차 스프린트 5건 모두 완료. 빌드 검증 OK (RAM 59.6%, Flash 43.7%). 디바이스 실 검증은 `pio run -t uploadfs` + `pio run -t upload` 후 monitor.htm 동작 확인 필요.
- 2026-05-09: 2차 진행 — CRC16 Modbus 토글, style.css 단일화(index/wifi 인라인 제거), 펌웨어 구조 분리(`dbg.h`/`version.h`/`wifi_manager`/`bridge`/`web_routes`). 빌드 OK (RAM 59.5%, Flash 43.7%). 모듈 분리로 인한 사이즈 오버헤드 사실상 0.
- 2026-05-09: SPIFFS 파티션 부족 발견 — d1_mini_lite 기본 ldscript는 SPIFFS 64KB뿐인데 디자인 현대화 후 data/ 60KB라 `SPIFFS_write error(-10001): File system is full` 발생. `board_build.ldscript = eagle.flash.1m128.ld`로 두 env 모두 변경하여 SPIFFS 128KB / sketch 한도 ~888KB로 재조정. 펌웨어 419KB이고 OTA 절반 한도 ~446KB라 OTA 여유 유지. `/api/status`에 `fs.{total,used,free,block_size}` + `flash.{id_size,real_size,sketch_used,sketch_free}` 추가, monitor 헤더에 `fs %` 표시. 빌드 OK (RAM 59.6%, Flash 47.0%, SPIFFS 이미지 131,072 bytes). **첫 USB 플래시 시 `pio run -e d1_mini_lite -t erase` 후 `-t upload && -t uploadfs` 권장** (파티션 레이아웃 변경으로 기존 SPIFFS 영역 호환 안됨).
- 2026-05-09: 3차 진행 — 프론트엔드 디자인 현대화 스프린트. `style.css` 디자인 토큰화(surface/accent/spacing/radius) + 공용 button/input variant. `index.htm` 카드 레이아웃 + 그라디언트 로고. `wifi.htm` 비밀번호 마스킹·표시 토글, RSSI 4단 막대·🔒 잠금 아이콘·신호세기 정렬, 토스트, 초기화 confirm. `monitor.htm` `/`·Esc·Space 단축키, substring 검색 바, preset 팝업 헤더. `safe-area-inset` + `100dvh` 모바일 보정. 펌웨어 변경 없음(데이터만), 빌드 검증 OK (RAM 59.5%, Flash 43.7%). `pio run -t uploadfs` 후 실 디바이스 UI 확인 필요.
