#include "web_routes.h"
#include "bridge.h"
#include "credentials.h"
#include "dbg.h"
#include "version.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPIFFSEditor.h>

AsyncWebServer server(80);
AsyncEventSource events("/events");

static const char *http_username = USER_NAME;
static const char *http_password = USER_PASSWD;

void webRoutesSetup()
{
  events.onConnect([](AsyncEventSourceClient *client)
                   { client->send("hello!", NULL, millis(), 1000); });
  server.addHandler(&events);
  server.addHandler(&ws); // bridge.cpp 가 소유한 WebSocket
  server.addHandler(new SPIFFSEditor(http_username, http_password));

  // ----- /api/status -------------------------------------------------------
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["version"] = EVERYESP_VERSION;
    doc["build"] = EVERYESP_BUILD;
    doc["hostname"] = hostName;
    doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
    doc["free_heap"] = ESP.getFreeHeap();
    doc["heap_frag"] = ESP.getHeapFragmentation();
    doc["chip_id"] = ESP.getChipId();
    doc["reset_reason"] = ESP.getResetReason();
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      doc["mode"] = "STA";
      doc["ssid"] = WiFi.SSID();
      doc["ip"] = WiFi.localIP().toString();
      doc["rssi"] = WiFi.RSSI();
    } else {
      doc["mode"] = "AP";
      doc["ssid"] = hostName;
      doc["ip"] = WiFi.softAPIP().toString();
    }
    doc["ws_clients"] = ws.count();
    doc["baud"] = bridgeGetBaud();
    JsonObject total = doc["ws_total"].to<JsonObject>();
    total["bin_frames"] = wsBinFramesTotal;
    total["bin_bytes"] = (double)wsBinBytesTotal;
    total["text_frames"] = wsTextFramesTotal;
    total["dropped"] = wsBinDroppedTotal;
    JsonObject recent = doc["ws_recent"].to<JsonObject>();
    recent["bin_frames"] = wsBinFrames;
    recent["bin_bytes"] = wsBinBytes;
    recent["max_len"] = wsBinMaxLen;
    recent["text_frames"] = wsTextFrames;
    recent["dropped"] = wsBinDropped;
    JsonObject flash = doc["flash"].to<JsonObject>();
    flash["id_size"] = ESP.getFlashChipSize();         // SDK가 인식한 크기
    flash["real_size"] = ESP.getFlashChipRealSize();   // 실제 칩 크기
    flash["sketch_used"] = ESP.getSketchSize();
    flash["sketch_free"] = ESP.getFreeSketchSpace();
    JsonObject fs = doc["fs"].to<JsonObject>();
    FSInfo info;
    if (SPIFFS.info(info)) {
      fs["total"] = info.totalBytes;
      fs["used"] = info.usedBytes;
      fs["free"] = info.totalBytes - info.usedBytes;
      fs["block_size"] = info.blockSize;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out); });

  // ----- /api/baud ---------------------------------------------------------
  // GET  → 현재 baud 반환
  // POST {"baud": <uint>}  → 적용 + SPIFFS 영속화
  server.on("/api/baud", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["baud"] = bridgeGetBaud();
    JsonArray allowed = doc["allowed"].to<JsonArray>();
    static const uint32_t kList[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 74880, 115200, 230400};
    for (size_t i = 0; i < sizeof(kList) / sizeof(kList[0]); i++) allowed.add(kList[i]);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out); });

  server.on(
      "/api/baud", HTTP_POST,
      [](AsyncWebServerRequest *request) { DBG_PRINTLN("api/baud"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        static String acc;
        if (total > 256)
        {
          request->send(413, "text/plain", "body too large");
          acc = "";
          return;
        }
        if (index == 0) acc = "";
        acc.reserve(total);
        for (size_t k = 0; k < len; k++) acc += (char)data[k];
        if (index + len < total) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, acc);
        acc = "";
        if (err)
        {
          request->send(400, "text/plain", "invalid JSON");
          return;
        }
        if (!doc["baud"].is<uint32_t>())
        {
          request->send(400, "text/plain", "missing baud");
          return;
        }
        uint32_t baud = doc["baud"].as<uint32_t>();
        if (!bridgeIsValidBaud(baud))
        {
          request->send(400, "text/plain", "unsupported baud");
          return;
        }
        if (!bridgeSaveBaud(baud))
        {
          request->send(500, "text/plain", "save failed");
          return;
        }
        bridgeApplyBaud(baud);
        JsonDocument res;
        res["baud"] = baud;
        String out;
        serializeJson(res, out);
        request->send(200, "application/json", out);
      });

  // ----- /scan + /wifistatus -----------------------------------------------
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
      request->send(200, "text/plain", "No networks found");
      return;
    }
    if (scanResult > 0) {
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

  // ----- /wifireset --------------------------------------------------------
  server.on("/wifireset", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    SPIFFS.remove("/config.ini");
    setupConnection();
    request->send(200, "text/plain", "Deleted ssid and password saved"); });

  // ----- /connect2ssid -----------------------------------------------------
  server.on(
      "/connect2ssid", HTTP_POST,
      [](AsyncWebServerRequest *request) { DBG_PRINTLN("connect2ssid"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        static String acc;
        if (total > 1024)
        {
          request->send(413, "text/plain", "body too large");
          acc = "";
          return;
        }
        if (index == 0) acc = "";
        acc.reserve(total);
        for (size_t k = 0; k < len; k++) acc += (char)data[k];
        if (index + len < total) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, acc);
        if (err)
        {
          DBG_PRINTF("connect2ssid JSON err: %s\n", err.c_str());
          request->send(400, "text/plain", "invalid JSON");
          acc = "";
          return;
        }
        String ssid = doc["ssid"].as<String>();
        String passwd = doc["passwd"].as<String>();
        if (ssid.length() == 0)
        {
          request->send(400, "text/plain", "missing ssid");
          acc = "";
          return;
        }
        File file = SPIFFS.open("/config.ini", "w");
        if (!file)
        {
          request->send(500, "text/plain", "config.ini open failed");
          acc = "";
          return;
        }
        file.print(ssid + ";");
        file.print(passwd);
        file.close();
        acc = "";
        request->send(200, "text/plain", "ssid and passwd saved");
        delay(100);
        ESP.restart();
      });

  // ----- /savepreset -------------------------------------------------------
  server.on(
      "/savepreset", HTTP_POST,
      [](AsyncWebServerRequest *request) { DBG_PRINTLN("savepreset"); },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        static String acc;
        if (total > 4096)
        {
          request->send(413, "text/plain", "preset too large");
          acc = "";
          return;
        }
        if (index == 0) acc = "";
        acc.reserve(total);
        for (size_t k = 0; k < len; k++) acc += (char)data[k];
        if (index + len < total) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, acc);
        if (err)
        {
          DBG_PRINTF("savepreset JSON err: %s\n", err.c_str());
          request->send(400, "text/plain", "invalid JSON");
          acc = "";
          return;
        }
        File file = SPIFFS.open("/preset.ini", "w");
        if (!file)
        {
          request->send(500, "text/plain", "preset.ini open failed");
          acc = "";
          return;
        }
        file.print(acc);
        file.close();
        request->send(200, "application/json", acc);
        acc = "";
      });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404); });
}
