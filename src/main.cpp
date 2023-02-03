#include <Arduino.h>
#include "credentials.h"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

String ssid;
String password;
const char *hostName = "everyESP485";
const char *http_username = USER_NAME;
const char *http_password = USER_PASSWD;

int scanResult = 0;

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
      delay(500);
      Serial.print(".");
      itt++;
      if (itt > 50)
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
    client->ping();
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_PONG)
  {
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len)
    {
      // the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < info->len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < info->len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if (info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
        if (info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if ((info->index + len) == info->len)
      {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if (info->final)
        {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          if (info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}

void setupWEBSOCKET()
{
  ws.onEvent(webSocketEvent);
  server.addHandler(&ws);
  Serial.println("WebSocket server started.");
}

void setupWEBPAGE()
{
  SPIFFS.begin();

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
        char origin[total];
        strncpy(origin, (char *)data, total);
        String temp = origin;
        uint8 commaIndex = temp.indexOf(",") - 1;
        uint8 endIndex = temp.indexOf("}") - 1;
        String ssid = temp.substring(9, commaIndex);
        String passwd = temp.substring(commaIndex + 11, endIndex);

        // ssid와 비번 저장
        File file = SPIFFS.open("/config.ini", "w");
        file.print(ssid + ";");
        file.print(passwd);
        file.close();
        setupCONNECTION();
        request->send(200, "text/plain", "ssid and passwd saved");
      });

  server.on(
      "/savepreset", HTTP_POST, [](AsyncWebServerRequest *request)
      { Serial.println("savepreset"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        char origin[total];
        strncpy(origin, (char *)data, total);
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
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len); });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                       {
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total); });

  server.begin();
}

void setup()
{
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  setupWEBSOCKET();
  setupWEBPAGE();
  MDNS.addService("http", "tcp", 80);
  setupCONNECTION();
}

#define bufferSize 8192
uint8_t buf[bufferSize];
uint16_t i = 0;
char rc;
void loop()
{
  ws.cleanupClients();

  while (Serial.available() > 0)
  {
    rc = Serial.read();
    buf[i] = rc;
    if (i < bufferSize - 1)
      i++;

    if (rc == '\n')
    {
      ws.binaryAll(buf, i);
      Serial.write(buf, i);
      i = 0;
    }
  }
}