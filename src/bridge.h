#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// RS485 ↔ WebSocket 브리지.
// - bridgeSetup(): WebSocket 핸들러 등록.
// - bridgeLoop(): loop()에서 매 iteration 호출. UART 비차단 수신 + idle gap 기반 flush
//   + heap 가드 backpressure + 5초 통계 로그.
//
// HTTP 서버에 ws 핸들러를 attach 하는 책임은 web_routes 쪽에 있다 (server.addHandler(&ws)).

extern AsyncWebSocket ws;

// 5초 윈도우 통계 (bridgeLoop에서 5초마다 0으로 리셋)
extern volatile uint32_t wsBinFrames;
extern volatile uint32_t wsBinBytes;
extern volatile uint32_t wsBinMaxLen;
extern volatile uint32_t wsTextFrames;
extern volatile uint32_t wsBinDropped;

// 부팅 이후 누적 통계 (/api/status 용)
extern volatile uint32_t wsBinFramesTotal;
extern volatile uint32_t wsTextFramesTotal;
extern volatile uint32_t wsBinDroppedTotal;
extern volatile uint64_t wsBinBytesTotal;

void bridgeSetup();
void bridgeLoop();

// RS485 (Serial) baud 관리.
// - bridgeLoadBaud(): SPIFFS `/baud.ini`에서 값을 읽어 반환. 파일 없거나 파싱 실패 시
//   기본값(BRIDGE_DEFAULT_BAUD).
// - bridgeApplyBaud(): Serial을 flush/end 후 새 baud로 begin. 현재 값 갱신만 수행.
// - bridgeSaveBaud(): SPIFFS에 영속화. 다음 부팅에도 유지된다.
// - bridgeGetBaud(): 현재 적용 중인 baud.
// - bridgeIsValidBaud(): 허용 범위/리스트 체크.
#define BRIDGE_DEFAULT_BAUD 9600UL

uint32_t bridgeLoadBaud();
void bridgeApplyBaud(uint32_t baud);
bool bridgeSaveBaud(uint32_t baud);
uint32_t bridgeGetBaud();
bool bridgeIsValidBaud(uint32_t baud);
