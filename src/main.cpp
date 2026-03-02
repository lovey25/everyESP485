#include <Arduino.h>
#include "credentials.h"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <ArduinoOTA.h>

#define LED 2
#define packTimeout 5 // ms (if nothing more on Serial, then send packet)
#define ENABLE_WEBSOCKET 1
#define ENABLE_WEBPAGE 1
#define DEBUG_HEX 0       // RS485 라인 오염 방지를 위해 기본 비활성화
#define RS485_CLEAN_BUS 1 // 1이면 RS485 라인에 디버그 문자열 출력 금지

#if RS485_CLEAN_BUS
#define DBG_PRINT(...)
#define DBG_PRINTLN(...)
#define DBG_PRINTF(...)
#else
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#endif

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

String ssid;
String password;
const char *hostName = "everyESP";
const char *http_username = USER_NAME;
const char *http_password = USER_PASSWD;

int scanResult = 0;
volatile uint32_t wsBinFrames = 0;
volatile uint32_t wsBinBytes = 0;
volatile uint32_t wsBinMaxLen = 0;
volatile uint32_t wsTextFrames = 0;
unsigned long wsLastLogMs = 0;

void setupCONNECTION()
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
  else
  {
    // 2. 저장 파일 불러오기
    DBG_PRINTLN("STA모드");
    ssid = file.readStringUntil(';');
    password = file.readStringUntil(';');
    DBG_PRINTLN(ssid);
    DBG_PRINTLN(password);

    // 3. 접속 시도
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    DBG_PRINTLN("\n");
    uint8 itt = 0;
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
    DBG_PRINTLN(WiFi.localIP()); // 할당받은 IP주소 표시
  }
}

void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    DBG_PRINTF("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    // client->ping();  // ping() causes reset - disabled for now
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    DBG_PRINTF("ws[%s][%u] disconnect\n", server->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    (void)server;
    (void)client;
    (void)arg;
    (void)data;
    (void)len;
  }
  else if (type == WS_EVT_PONG)
  {
    (void)server;
    (void)client;
    (void)arg;
    (void)data;
    (void)len;
  }
  else if (type == WS_EVT_DATA)
  {
    if (len == 0)
    {
      return;
    }
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len)
    {
      // the whole message is in a single frame and we got all of it's data
      if (info->opcode == WS_TEXT)
      {
        wsTextFrames++;
        Serial.write(data, len);
      }
      else
      {
        wsBinFrames++;
        wsBinBytes += info->len;
        if (info->len > wsBinMaxLen)
        {
          wsBinMaxLen = info->len;
        }
        Serial.write(data, len);
      }
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (info->opcode == WS_TEXT)
      {
        wsTextFrames++;
        Serial.write(data, len);
      }
      else
      {
        wsBinFrames++;
        wsBinBytes += len;
        if (len > wsBinMaxLen)
        {
          wsBinMaxLen = len;
        }
        Serial.write(data, len);
      }
    }
  }
}

void setupWEBSOCKET()
{
  if (ENABLE_WEBSOCKET)
  {
    ws.onEvent(webSocketEvent);
    server.addHandler(&ws);
    DBG_PRINTLN("WebSocket server started.");
  }
  else
  {
    DBG_PRINTLN("WebSocket disabled.");
  }
}

void setupWEBPAGE()
{
  if (!ENABLE_WEBPAGE)
  {
    return;
  }

  events.onConnect([](AsyncEventSourceClient *client)
                   { client->send("hello!", NULL, millis(), 1000); });
  server.addHandler(&events);
  server.addHandler(new SPIFFSEditor(http_username, http_password));

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    WiFi.scanNetworks(/*async=*/true, /*hidden=*/false);
    request->send(200, "text/plain", "scan requested"); });

  server.on("/wifistatus", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String scanSsid;
    int32_t scanRssi;
    uint8_t scanEncryptionType;
    uint8_t *scanBssid;
    int32_t scanChannel;
    bool scanHidden;
    int scanResult = 0;
    String strResult = "{";  
    
    scanResult = WiFi.scanComplete();
    
    if (scanResult == 0) {
      request->send(200, "text/plain","No networks found");
    } else if (scanResult > 0) {
      // Print unsorted scan results
      for (int8_t i = 0; i < scanResult; i++) {
        WiFi.getNetworkInfo(i, scanSsid, scanEncryptionType, scanRssi, scanBssid, scanChannel, scanHidden);
        String phyMode;
        const char *wps = "";
        char buffer[200];
        sprintf(buffer, PSTR("\"%d\": {\"channel\":\"%02d\", \"power\":\"%ddBm\", \"scanEncryptionType\":\"%c\", \"scanHidden\":\"%c\", \"phyMode\":\"%-11s\",\"wps\":\"%3S\",\"ssid\":\"%s\"}"),
                i, scanChannel, scanRssi, (scanEncryptionType == ENC_TYPE_NONE) ? ' ' : '*', scanHidden ? 'H' : 'V', phyMode.c_str(), wps, scanSsid.c_str());
        
        strResult += buffer;
        strResult += ",";
    }
  } else {
    DBG_PRINTF(PSTR("WiFi scan error %d"), scanResult);
  }
  strResult += "}";
  strResult.replace("},}", "}}");

  request->send(200, "application/json", strResult); });

  server.on("/wifireset", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    SPIFFS.remove("/config.ini");
    setupCONNECTION();
    request->send(200, "text/plain", "Deleted ssdi and password saved"); });

  server.on(
      "/connect2ssid", HTTP_POST, [](AsyncWebServerRequest *request)
      { DBG_PRINTLN("connect2ssid"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        char origin[total + 1];
        memcpy(origin, (char *)data, total);
        origin[total] = '\0';
        String temp = origin;
        uint8 commaIndex = temp.indexOf(",") - 1;
        uint8 endIndex = temp.indexOf("}") - 1;
        String ssid = temp.substring(9, commaIndex);
        String passwd = temp.substring(commaIndex + 12, endIndex);
        DBG_PRINTLN(temp);
        DBG_PRINTLN(ssid);
        DBG_PRINTLN(passwd);

        // ssid와 비번 저장
        File file = SPIFFS.open("/config.ini", "w");
        file.print(ssid + ";");
        file.print(passwd);
        file.close();
        request->send(200, "text/plain", "ssid and passwd saved");
        ESP.restart();
      });

  server.on(
      "/savepreset", HTTP_POST, [](AsyncWebServerRequest *request)
      { DBG_PRINTLN("savepreset"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        char origin[total + 1];
        memcpy(origin, (char *)data, total);
        origin[total] = '\0';
        String temp = origin;
        uint8 endIndex = temp.indexOf("}") + 1;
        String preset = temp.substring(0, endIndex);

        File file = SPIFFS.open("/preset.ini", "w");
        file.print(preset);
        file.close();
        request->send(200, "text/plain", preset);
      });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(404); });
}

void setupHTTPMinimal()
{
  if (ENABLE_WEBPAGE)
  {
    return;
  }

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "ok"); });

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(404); });
}

void setupOTA()
{
  // ArduinoOTA.setPort(8266);           // Port defaults to 8266
  // ArduinoOTA.setHostname(hostName);   // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setPassword("admin"); // No authentication by default

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]()
                     { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]()
                   { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota"); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota"); });

  ArduinoOTA.begin();
  DBG_PRINTLN("OTA Ready");
}

void setup()
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  Serial.begin(9600);
  // Serial.setDebugOutput(true);
  DBG_PRINTF("Reset reason: %s\n", ESP.getResetReason().c_str());

  DBG_PRINTLN("setup: spiffs");
  if (!SPIFFS.begin())
  {
    DBG_PRINTLN("SPIFFS begin failed");
  }

  DBG_PRINTLN("setup: websocket");
  setupWEBSOCKET();
  DBG_PRINTLN("setup: webpage");
  setupWEBPAGE();
  DBG_PRINTLN("setup: http minimal");
  setupHTTPMinimal();
  DBG_PRINTLN("setup: connection");
  setupCONNECTION();

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

#define bufferSize 8192
uint8_t buf[bufferSize];
uint16_t i = 0;
char rc;
void loop()
{
  ArduinoOTA.handle();
  if (ENABLE_WEBSOCKET)
  {
    ws.cleanupClients();
  }

  if (millis() - wsLastLogMs >= 5000)
  {
    if (wsBinFrames > 0 || wsTextFrames > 0)
    {
      DBG_PRINTF("WS stats: bin=%u bytes=%u max=%u text=%u\n",
                 wsBinFrames, wsBinBytes, wsBinMaxLen, wsTextFrames);
    }
    wsBinFrames = 0;
    wsBinBytes = 0;
    wsBinMaxLen = 0;
    wsTextFrames = 0;
    wsLastLogMs = millis();
  }

  if (Serial.available())
  {
    const unsigned long loopStartMs = millis();
    i = 0; // 버퍼 인덱스 초기화
    while (1)
    {
      if (Serial.available())
      {
        if (i < bufferSize)
        {
          buf[i] = (uint8_t)Serial.read(); // read byte from UART
          i++;
        }
        else
        {
          Serial.read(); // 버퍼 초과시 데이터 버림
        }

        if ((i % 64) == 0)
        {
          yield();
          if (millis() - loopStartMs > 1000)
          {
            break;
          }
        }
      }
      else
      {
        delay(packTimeout); // delayMicroseconds(packTimeout);
        if (!Serial.available())
        {
          break;
        }
      }
    }
  }

  if (i > 0)
  {
#if DEBUG_HEX
    // HEX 값 출력
    DBG_PRINT("HEX:");
    for (uint16_t j = 0; j < i; j++)
    {
      DBG_PRINTF("%02X ", buf[j]);
    }
    DBG_PRINTLN();
#endif

#if ENABLE_WEBSOCKET
    ws.binaryAll(buf, i); // now send to WiFi:
#endif
    i = 0;
  }

  yield();
}