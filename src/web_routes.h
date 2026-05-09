#pragma once

#include <ESPAsyncWebServer.h>

// HTTP 서버와 SSE(events) 객체. main.cpp에서 server.begin() 호출, OTA 진행 상황은
// events.send(..., "ota") 로 푸시한다.
extern AsyncWebServer server;
extern AsyncEventSource events;

// 라우트 + WebSocket(addHandler) + 이벤트 소스 등록.
// SPIFFS는 main에서 미리 begin() 되어 있어야 한다.
void webRoutesSetup();
