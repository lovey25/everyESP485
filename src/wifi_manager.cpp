#include "wifi_manager.h"
#include "dbg.h"
#include "version.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>

#define LED 2

void setupConnection()
{
  // 1. 저장된 SSID와 비번 확인
  File file = SPIFFS.open("/config.ini", "r");
  if (!file || file.isDirectory())
  {
    // 1-1 저장된 ssid가 없으면 AP모드 실행
    DBG_PRINTLN("AP모드");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(hostName);
    return;
  }

  // 2. 저장 파일 불러오기
  DBG_PRINTLN("STA모드");
  String ssid = file.readStringUntil(';');
  String password = file.readStringUntil(';');
  DBG_PRINTLN(ssid);
  DBG_PRINTLN(password);

  // 3. 접속 시도
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  DBG_PRINTLN("\n");
  uint8_t itt = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED, LOW);
    delay(500);
    DBG_PRINT(".");
    digitalWrite(LED, HIGH);
    delay(500);
    itt++;
    if (itt > 15)
    {
      DBG_PRINTF("STA: Failed!\n");
      WiFi.disconnect(false);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(hostName);
      return;
    }
  }
  DBG_PRINTLN();
  DBG_PRINTLN("Connected!");
  DBG_PRINT("IP address:\t");
  DBG_PRINTLN(WiFi.localIP());
}
