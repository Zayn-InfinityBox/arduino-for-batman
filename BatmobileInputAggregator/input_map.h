#pragma once

#include <Arduino.h>

#define MCP_CS_PIN 9
#define MCP_INT_PIN 2
#define MCP_CRYSTAL_AUTO 0
#define MCP_CRYSTAL MCP_16MHZ
#define CRYSTAL_PROBE_MS 1500
#define USE_HARDWARE_FILTERS 1
#define DEBUG_SERIAL 1
#define SERIAL_BAUD 115200

static const uint32_t OUT_CAN_ID = 0x18CF0080UL;
static const uint16_t OUT_PERIOD_MS = 250;
static const uint8_t OUT_GUARD_BYTE = 0xA5;

static const uint32_t SRC_PGN_A = 0xEF01UL;
static const uint32_t SRC_PGN_B = 0xEF02UL;
static const uint16_t SRC_TIMEOUT_MS = 1000;
#define CLEAR_TOGGLES_ON_TIMEOUT 1

#define IN(n) ((uint8_t)((n) - 1))
#define HSIN(n) ((uint8_t)(37 + (n)))
static const uint8_t TARGET_COUNT = 44;

enum : uint8_t { SRC_A = 0, SRC_B = 1, SRC_COUNT = 2 };
enum : uint8_t { MODE_FOLLOW, MODE_TOGGLE };
enum : uint8_t { MATCH_BITS, MATCH_EQUAL };

#define ANY_NONZERO 0xFFFFu

#ifndef ROTARY_AS_BITMASK
#define ROTARY_AS_BITMASK 1
#endif

#if ROTARY_AS_BITMASK
#define POS(n) ((uint16_t)1u << ((n) - 1))
#else
#define POS(n) ((uint16_t)(n))
#endif

struct InputMapEntry {
  uint8_t source;
  uint8_t byteIndex;
  uint8_t width;
  uint8_t match;
  uint16_t matchValue;
  uint8_t target;
  uint8_t mode;
};

#ifdef AGG_TEST_MAP
#include AGG_TEST_MAP
#else

static const InputMapEntry INPUT_MAP[] PROGMEM = {
    {SRC_A, 0, 1, MATCH_BITS, ANY_NONZERO, IN(21), MODE_FOLLOW},  // low beams
    {SRC_A, 1, 1, MATCH_BITS, ANY_NONZERO, IN(22), MODE_FOLLOW},  // high beams
    {SRC_A, 2, 1, MATCH_BITS, ANY_NONZERO, IN(19), MODE_FOLLOW},  // horn
    {SRC_A, 3, 1, MATCH_BITS, ANY_NONZERO, IN(17), MODE_FOLLOW},  // exhaust loud
    {SRC_A, 4, 1, MATCH_BITS, ANY_NONZERO, IN(18), MODE_FOLLOW},  // exhaust quiet
    {SRC_A, 5, 1, MATCH_BITS, ANY_NONZERO, IN(20), MODE_FOLLOW},  // hazards

    {SRC_B, 0, 2, MATCH_EQUAL, POS(3), IN(26), MODE_FOLLOW},  // interior
    {SRC_B, 0, 2, MATCH_EQUAL, POS(7), IN(27), MODE_FOLLOW},  // engine / jet
    {SRC_B, 2, 2, MATCH_EQUAL, POS(5), IN(25), MODE_FOLLOW},  // smoke power
    {SRC_B, 4, 2, MATCH_EQUAL, POS(3), IN(23), MODE_FOLLOW},  // wipers low
    {SRC_B, 4, 2, MATCH_EQUAL, POS(5), IN(24), MODE_FOLLOW},  // wipers high
};

#endif

static const uint8_t MAP_COUNT = sizeof(INPUT_MAP) / sizeof(INPUT_MAP[0]);
