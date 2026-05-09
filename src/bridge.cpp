#include "bridge.h"
#include "dbg.h"

#include <Arduino.h>
#include <FS.h>

// --------------------------------------------------------------------------
// 튜닝 상수
// --------------------------------------------------------------------------
#define BRIDGE_BUF_SIZE 8192
#define PACK_TIMEOUT_US 5000UL  // RS485 패킷 종료 판정용 idle 시간 (us)
#define MAX_ACCUM_US 30000UL    // 연속 트래픽 하 최대 누적 시간 (us).
                                // idle gap이 안 생기는 빽빽한 스트림에서 첫 바이트
                                // 부터 이 시간 경과하면 강제 flush → UI 실시간 갱신.
#define WS_HEAP_GUARD 12288     // free heap 임계치 (이하면 WS 송신 drop).
                                // 1KB 메시지 binaryAll의 라이브러리 alloc(~1.5KB) +
                                // 다음 RX 누적 분 + 안전 마진을 모두 흡수하는 값.

AsyncWebSocket ws("/ws");

volatile uint32_t wsBinFrames = 0;
volatile uint32_t wsBinBytes = 0;
volatile uint32_t wsBinMaxLen = 0;
volatile uint32_t wsTextFrames = 0;
volatile uint32_t wsBinDropped = 0;

volatile uint32_t wsBinFramesTotal = 0;
volatile uint32_t wsTextFramesTotal = 0;
volatile uint32_t wsBinDroppedTotal = 0;
volatile uint64_t wsBinBytesTotal = 0;

static uint8_t s_buf[BRIDGE_BUF_SIZE];     // RS485 → WS 송신 누적 버퍼
static uint8_t s_wsRxBuf[BRIDGE_BUF_SIZE]; // WS → RS485 fragment 재조립
static uint16_t s_idx = 0;
static unsigned long s_lastByteUs = 0;
static unsigned long s_firstByteUs = 0;
static unsigned long s_wsLastLogMs = 0;
static uint32_t s_baud = BRIDGE_DEFAULT_BAUD;

// 허용되는 표준 baud rate 목록. UI 드롭다운과 일치시킨다.
static const uint32_t kAllowedBauds[] = {
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 74880, 115200, 230400};

// --------------------------------------------------------------------------
// WebSocket 이벤트 핸들러
// --------------------------------------------------------------------------
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    DBG_PRINTF("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    // client->ping();  // ping() causes reset on this stack — disabled
    return;
  }
  if (type == WS_EVT_DISCONNECT)
  {
    DBG_PRINTF("ws[%s][%u] disconnect\n", server->url(), client->id());
    return;
  }
  if (type != WS_EVT_DATA || len == 0)
  {
    return;
  }

  AwsFrameInfo *info = (AwsFrameInfo *)arg;

  if (info->final && info->index == 0 && info->len == len)
  {
    // 단일 프레임에 메시지 전체 도달 → 즉시 RS485로 송신
    if (info->opcode == WS_TEXT)
    {
      wsTextFrames++;
      wsTextFramesTotal++;
    }
    else
    {
      wsBinFrames++;
      wsBinFramesTotal++;
      wsBinBytes += info->len;
      wsBinBytesTotal += info->len;
      if (info->len > wsBinMaxLen) wsBinMaxLen = info->len;
    }
    Serial.write(data, len);
    return;
  }

  // fragmented 메시지: s_wsRxBuf에 누적, final 도달 시점에만 일괄 송신.
  // (fragment마다 즉시 Serial.write 하면 RS485 패킷이 분할 출력되어 슬레이브와 충돌)
  if ((info->index + len) <= sizeof(s_wsRxBuf))
  {
    memcpy(s_wsRxBuf + info->index, data, len);
  }
  bool isLastChunk = info->final && ((info->index + len) == info->len);
  if (!isLastChunk) return;

  if (info->len <= sizeof(s_wsRxBuf))
  {
    Serial.write(s_wsRxBuf, info->len);
  }
  if (info->opcode == WS_TEXT)
  {
    wsTextFrames++;
    wsTextFramesTotal++;
  }
  else
  {
    wsBinFrames++;
    wsBinFramesTotal++;
    wsBinBytes += info->len;
    wsBinBytesTotal += info->len;
    if (info->len > wsBinMaxLen) wsBinMaxLen = info->len;
  }
}

// --------------------------------------------------------------------------
// Baud rate 관리
// --------------------------------------------------------------------------
bool bridgeIsValidBaud(uint32_t baud)
{
  for (size_t i = 0; i < sizeof(kAllowedBauds) / sizeof(kAllowedBauds[0]); i++)
  {
    if (kAllowedBauds[i] == baud) return true;
  }
  return false;
}

uint32_t bridgeGetBaud() { return s_baud; }

uint32_t bridgeLoadBaud()
{
  File f = SPIFFS.open("/baud.ini", "r");
  if (!f || f.isDirectory())
  {
    if (f) f.close();
    return BRIDGE_DEFAULT_BAUD;
  }
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  long v = s.toInt();
  if (v <= 0 || !bridgeIsValidBaud((uint32_t)v)) return BRIDGE_DEFAULT_BAUD;
  return (uint32_t)v;
}

bool bridgeSaveBaud(uint32_t baud)
{
  if (!bridgeIsValidBaud(baud)) return false;
  File f = SPIFFS.open("/baud.ini", "w");
  if (!f) return false;
  f.print(baud);
  f.close();
  return true;
}

void bridgeApplyBaud(uint32_t baud)
{
  // 진행 중인 RS485 송신을 비우고 UART 재초기화. setRxBufferSize는 begin 전에
  // 호출되어야 효과가 있으므로 end → setRxBufferSize → begin 순서를 유지한다.
  Serial.flush();
  Serial.end();
  Serial.setRxBufferSize(4096);
  Serial.begin(baud);
  s_baud = baud;
  // 누적 RX 버퍼는 이전 baud 기준이라 깨졌을 수 있으므로 초기화.
  s_idx = 0;
  s_firstByteUs = 0;
  s_lastByteUs = 0;
}

// --------------------------------------------------------------------------
// 외부 API
// --------------------------------------------------------------------------
void bridgeSetup()
{
  ws.onEvent(onWsEvent);
  DBG_PRINTLN("WebSocket handler registered.");
}

void bridgeLoop()
{
  ws.cleanupClients();

  // 5초 윈도우 통계 로그
  if (millis() - s_wsLastLogMs >= 5000)
  {
    if (wsBinFrames > 0 || wsTextFrames > 0 || wsBinDropped > 0)
    {
      DBG_PRINTF("WS stats: bin=%u bytes=%u max=%u text=%u dropped=%u heap=%u\n",
                 wsBinFrames, wsBinBytes, wsBinMaxLen, wsTextFrames,
                 wsBinDropped, ESP.getFreeHeap());
    }
    wsBinFrames = 0;
    wsBinBytes = 0;
    wsBinMaxLen = 0;
    wsTextFrames = 0;
    wsBinDropped = 0;
    s_wsLastLogMs = millis();
  }

  // 비차단 수신
  while (Serial.available() && s_idx < BRIDGE_BUF_SIZE)
  {
    if (s_idx == 0) s_firstByteUs = micros();
    s_buf[s_idx++] = (uint8_t)Serial.read();
    s_lastByteUs = micros();
  }
  while (Serial.available() && s_idx >= BRIDGE_BUF_SIZE)
  {
    Serial.read();
    wsBinDropped++;
    wsBinDroppedTotal++;
  }

  bool shouldFlush = (s_idx > 0) &&
                     ((s_idx >= BRIDGE_BUF_SIZE) ||
                      ((micros() - s_lastByteUs) >= PACK_TIMEOUT_US) ||
                      ((micros() - s_firstByteUs) >= MAX_ACCUM_US));

  if (!shouldFlush) return;

  if (ws.count() > 0)
  {
    // backpressure: free heap이 임계 미만이면 drop
    if (ESP.getFreeHeap() < WS_HEAP_GUARD)
    {
      wsBinDropped += s_idx;
      wsBinDroppedTotal += s_idx;
    }
    else
    {
      ws.binaryAll(s_buf, s_idx);
    }
  }
  s_idx = 0;
}
