#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>

#include "bridge.h"
#include "dbg.h"
#include "version.h"
#include "web_routes.h"
#include "wifi_manager.h"

#define LED 2

const char *hostName = "everyESP";

static void setupOTA()
{
  ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress / (total / 100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if (error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if (error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if (error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if (error == OTA_END_ERROR) events.send("End Failed", "ota");
  });

  ArduinoOTA.begin();
  DBG_PRINTLN("OTA Ready");
}

void setup()
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  // SPIFFS는 baud 로드를 위해 Serial.begin 보다 먼저 마운트.
  if (!SPIFFS.begin())
  {
    // SPIFFS 실패 시는 기본 baud로 폴백.
  }

  // bridgeApplyBaud가 setRxBufferSize(4096) → Serial.begin을 처리한다.
  // 기본 256B → 4KB로 키워 1KB 패킷이 binaryAll 동기 처리 사이에 RX FIFO를 넘기는
  // overrun(0xFF) 패턴 방지.
  uint32_t baud = bridgeLoadBaud();
  bridgeApplyBaud(baud);
  DBG_PRINTF("Reset reason: %s\n", ESP.getResetReason().c_str());
  DBG_PRINTF("Serial baud: %u\n", baud);

  DBG_PRINTLN("setup: bridge");
  bridgeSetup();
  DBG_PRINTLN("setup: web routes");
  webRoutesSetup();
  DBG_PRINTLN("setup: connection");
  setupConnection();

  DBG_PRINTLN("setup: server begin");
  server.begin();

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
  {
    if (MDNS.begin(hostName))
    {
      MDNS.addService(hostName, "tcp", 80);
    }
    setupOTA();
  }
}

void loop()
{
  ArduinoOTA.handle();
  bridgeLoop();
  yield();
}
