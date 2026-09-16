#include "aggregator.h"

#include <string.h>

static uint8_t g_srcData[SRC_COUNT][8];
static uint32_t g_srcLastRx[SRC_COUNT];
static bool g_srcValid[SRC_COUNT];
static uint8_t g_outBits[6];

static const uint8_t MAP_BITMAP_BYTES = (MAP_COUNT + 7) / 8;
static uint8_t g_prevActive[MAP_BITMAP_BYTES];
static uint8_t g_toggleState[MAP_BITMAP_BYTES];

static inline bool bitmapGet(const uint8_t *map, uint8_t i) {
  return ((map[i >> 3] >> (i & 7)) & 1u) != 0;
}

static inline void bitmapSet(uint8_t *map, uint8_t i, bool value) {
  const uint8_t mask = (uint8_t)(1u << (i & 7));
  if (value) {
    map[i >> 3] |= mask;
  } else {
    map[i >> 3] &= (uint8_t)~mask;
  }
}

static inline void bitmapFlip(uint8_t *map, uint8_t i) {
  map[i >> 3] ^= (uint8_t)(1u << (i & 7));
}

static inline void setTargetBit(uint8_t target) {
  if (target >= TARGET_COUNT) return;
  g_outBits[target >> 3] |= (uint8_t)(0x80u >> (target & 7));
}

static inline InputMapEntry mapRow(uint8_t i) {
  InputMapEntry e;
  memcpy_P(&e, &INPUT_MAP[i], sizeof(e));
  return e;
}

uint32_t aggMatchKeyFromId(uint32_t canId) {
  return (canId >> 8) & 0x3FFFFUL;
}

static int8_t sourceIndexForKey(uint32_t key) {
  if (key == SRC_PGN_A) return SRC_A;
  if (key == SRC_PGN_B) return SRC_B;
  return -1;
}

void aggReset() {
  memset(g_srcData, 0, sizeof(g_srcData));
  memset(g_srcLastRx, 0, sizeof(g_srcLastRx));
  memset(g_srcValid, 0, sizeof(g_srcValid));
  memset(g_outBits, 0, sizeof(g_outBits));
  memset(g_prevActive, 0, sizeof(g_prevActive));
  memset(g_toggleState, 0, sizeof(g_toggleState));
}

bool aggHandleFrame(uint32_t canId, const uint8_t *data, uint8_t len, uint32_t nowMs) {
  const int8_t source = sourceIndexForKey(aggMatchKeyFromId(canId));
  if (source < 0) return false;

  if (len > 8) len = 8;
  memset(g_srcData[source], 0, sizeof(g_srcData[source]));
  memcpy(g_srcData[source], data, len);
  g_srcLastRx[source] = nowMs;
  g_srcValid[source] = true;
  return true;
}

void aggInjectByte(uint8_t source, uint8_t byteIndex, uint8_t value, uint32_t nowMs) {
  if (source >= SRC_COUNT || byteIndex > 7) return;
  g_srcData[source][byteIndex] = value;
  g_srcLastRx[source] = nowMs;
  g_srcValid[source] = true;
}

void aggTouchSource(uint8_t source, uint32_t nowMs) {
  if (source >= SRC_COUNT || !g_srcValid[source]) return;
  g_srcLastRx[source] = nowMs;
}

static void clearTogglesForSource(uint8_t source) {
  for (uint8_t i = 0; i < MAP_COUNT; i++) {
    if (mapRow(i).source != source) continue;
    bitmapSet(g_toggleState, i, false);
    bitmapSet(g_prevActive, i, false);
  }
}

uint8_t aggExpireStaleSources(uint32_t nowMs) {
  if (SRC_TIMEOUT_MS == 0) return 0;

  uint8_t expired = 0;
  for (uint8_t s = 0; s < SRC_COUNT; s++) {
    if (!g_srcValid[s]) continue;
    if ((uint32_t)(nowMs - g_srcLastRx[s]) < SRC_TIMEOUT_MS) continue;

    g_srcValid[s] = false;
    memset(g_srcData[s], 0, sizeof(g_srcData[s]));
#if CLEAR_TOGGLES_ON_TIMEOUT
    clearTogglesForSource(s);
#endif
    expired |= (uint8_t)(1u << s);
  }
  return expired;
}

void aggBuildPayload(uint8_t out[8]) {
  memset(g_outBits, 0, sizeof(g_outBits));

  for (uint8_t i = 0; i < MAP_COUNT; i++) {
    const InputMapEntry e = mapRow(i);

    bool active = false;
    if (e.source < SRC_COUNT && e.width >= 1 && (uint16_t)e.byteIndex + e.width <= 8 &&
        g_srcValid[e.source]) {
      uint16_t value = g_srcData[e.source][e.byteIndex];
      if (e.width == 2) value |= (uint16_t)g_srcData[e.source][e.byteIndex + 1] << 8;
      active = (e.match == MATCH_EQUAL) ? (value == e.matchValue)
                                        : ((value & e.matchValue) != 0);
    }

    if (e.mode == MODE_TOGGLE) {
      if (active && !bitmapGet(g_prevActive, i)) bitmapFlip(g_toggleState, i);
      bitmapSet(g_prevActive, i, active);
      active = bitmapGet(g_toggleState, i);
    }

    if (active) setTargetBit(e.target);
  }

  g_outBits[5] &= 0xF0;
  memcpy(out, g_outBits, 6);
  out[6] = OUT_GUARD_BYTE;
  out[7] = 0x00;
}
