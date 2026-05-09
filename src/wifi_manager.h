#pragma once

// SPIFFS의 /config.ini를 읽어 STA 접속을 시도하고, 실패 시 AP 모드로 fallback.
// /config.ini 형식: "ssid;password"
void setupConnection();
