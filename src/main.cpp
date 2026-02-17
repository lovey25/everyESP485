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
    Serial.println("AP모드");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(hostName);
    return;
  }
  else
  {
    // 2. 저장 파일 불러오기
    Serial.println("STA모드");
    ssid = file.readStringUntil(';');
    password = file.readStringUntil(';');
    Serial.println(ssid);
    Serial.println(password);

    // 3. 접속 시도
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("\n");
    uint8 itt = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      digitalWrite(LED, LOW);
      delay(500);
      Serial.print(".");
      digitalWrite(LED, HIGH);
      delay(500);
      itt++;
      if (itt > 15)
      {
        Serial.printf("STA: Failed!\n");
        WiFi.disconnect(false);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(hostName);
        return;
      }
    }
    Serial.println('\n');
    Serial.println("Connected!");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP()); // 할당받은 IP주소 표시
  }
}

void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    // client->ping();  // ping() causes reset - disabled for now
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
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
      }
      else
      {
        wsBinFrames++;
        wsBinBytes += info->len;
        if (info->len > wsBinMaxLen)
        {
          wsBinMaxLen = info->len;
        }
      }
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (info->opcode == WS_TEXT)
      {
        wsTextFrames++;
      }
      else
      {
        wsBinFrames++;
        wsBinBytes += len;
        if (len > wsBinMaxLen)
        {
          wsBinMaxLen = len;
        }
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
    Serial.println("WebSocket server started.");
  }
  else
  {
    Serial.println("WebSocket disabled.");
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
        sprintf(buffer, PSTR("\"%d\": {\"channel\":\"%02d\"\, \"power\":\"%ddBm\"\, \"scanEncryptionType\":\"%c\"\, \"scanHidden\":\"%c\"\, \"phyMode\":\"%-11s\"\,\"wps\":\"%3S\"\,\"ssid\":\"%s\"}"), 
                        i, scanChannel, scanRssi, (scanEncryptionType == ENC_TYPE_NONE) ? ' ' : '*', scanHidden ? 'H' : 'V', phyMode.c_str(), wps, scanSsid.c_str());
        
        strResult += buffer;
        strResult += ",";
    }
  } else {
    Serial.printf(PSTR("WiFi scan error %d"), scanResult);
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
      { Serial.println("connect2ssid"); },
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
        Serial.println(temp);
        Serial.println(ssid);
        Serial.println(passwd);

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
      { Serial.println("savepreset"); },
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
                    {
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404); });
  server.onFileUpload([](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
                      {
    if(!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.write(data, len);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len); });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                       {
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.write(data, len);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total); });
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
                    {
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    request->send(404); });
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
  Serial.println("OTA Ready");
}

void setup()
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  Serial.begin(74880);
  // Serial.setDebugOutput(true);
  Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());

  Serial.println("setup: spiffs");
  if (!SPIFFS.begin())
  {
    Serial.println("SPIFFS begin failed");
  }

  Serial.println("setup: websocket");
  setupWEBSOCKET();
  Serial.println("setup: webpage");
  setupWEBPAGE();
  Serial.println("setup: http minimal");
  setupHTTPMinimal();
  Serial.println("setup: connection");
  setupCONNECTION();

  Serial.println("setup: server begin");
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
      Serial.printf("WS stats: bin=%u bytes=%u max=%u text=%u\n",
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
    while (1)
    {
      if (Serial.available())
      {
        buf[i] = (char)Serial.read(); // read char from UART
        if (i < bufferSize - 1)
          i++;
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

  if (ENABLE_WEBSOCKET && i > 0)
  {
    ws.binaryAll(buf, i); // now send to WiFi:
    i = 0;
  }

  yield();
}