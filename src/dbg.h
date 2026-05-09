#pragma once

#include <Arduino.h>

// RS485 라인 오염 방지: 1이면 RS485 라인(=Serial)에 디버그 문자열 출력 금지.
// 디버그 출력은 항상 DBG_PRINT* 매크로로 통일하고, 이 플래그로 일괄 차단한다.
#ifndef RS485_CLEAN_BUS
#define RS485_CLEAN_BUS 1
#endif

#if RS485_CLEAN_BUS
#define DBG_PRINT(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#define DBG_PRINTF(...) ((void)0)
#else
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#endif
