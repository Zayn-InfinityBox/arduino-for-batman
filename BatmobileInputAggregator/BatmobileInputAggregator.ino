#include <SPI.h>
#include <mcp_can.h>

#include "aggregator.h"
#include "input_map.h"

MCP_CAN CAN0(MCP_CS_PIN);

static uint32_t g_nextSendMs;

#if DEBUG_SERIAL
static bool g_injecting;
#endif

#if MCP_CRYSTAL_AUTO
static bool hearsTrafficAt(uint8_t crystal) {
  if (CAN0.begin(MCP_ANY, CAN_250KBPS, crystal) != CAN_OK) return false;
  CAN0.setMode(MCP_LISTENONLY);

  const uint32_t deadline = millis() + CRYSTAL_PROBE_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    if (CAN0.checkReceive() != CAN_MSGAVAIL) continue;

    unsigned long rxId = 0;
    unsigned char len = 0;
    unsigned char buf[8];
    if (CAN0.readMsgBuf(&rxId, &len, buf) == CAN_OK) return true;
  }
  return false;
}

static uint8_t detectCrystal() {
  const uint8_t candidates[] = {MCP_16MHZ, MCP_8MHZ};
  for (uint8_t i = 0; i < 2; i++) {
    if (!hearsTrafficAt(candidates[i])) continue;
#if DEBUG_SERIAL
    Serial.print(F("Crystal detected: "));
    Serial.println(candidates[i] == MCP_16MHZ ? F("16 MHz") : F("8 MHz"));
#endif
    return candidates[i];
  }
#if DEBUG_SERIAL
  Serial.print(F("No traffic at either rate - is the bus live? Falling back to "));
  Serial.println(MCP_CRYSTAL == MCP_16MHZ ? F("16 MHz") : F("8 MHz"));
#endif
  return MCP_CRYSTAL;
}
#endif

static void pollReceive() {
  while (CAN0.checkReceive() == CAN_MSGAVAIL) {
    unsigned long rxId = 0;
    unsigned char len = 0;
    unsigned char buf[8];

    if (CAN0.readMsgBuf(&rxId, &len, buf) != CAN_OK) return;
    if (!(rxId & 0x80000000UL)) continue;

    if (aggHandleFrame((uint32_t)rxId, buf, len, millis())) {
#if DEBUG_SERIAL
      g_injecting = false;
#endif
    }
  }
}

static void sendAggregatedFrame() {
  uint8_t data[8];
  aggBuildPayload(data);
  CAN0.sendMsgBuf(OUT_CAN_ID, 1, 8, data);

#if DEBUG_SERIAL
  static uint8_t lastSent[8];
  static bool everSent = false;
  if (!everSent || memcmp(lastSent, data, 8) != 0) {
    everSent = true;
    memcpy(lastSent, data, 8);
    Serial.print(F("18CF0080  "));
    for (uint8_t i = 0; i < 8; i++) {
      if (data[i] < 0x10) Serial.write('0');
      Serial.print(data[i], HEX);
      Serial.write(' ');
    }
    Serial.println();
  }
#endif
}

#if DEBUG_SERIAL
static void pollSerialInjector() {
  static char line[16];
  static uint8_t len = 0;

  while (Serial.available()) {
    const char c = (char)Serial.read();

    if (c != '\n' && c != '\r') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    if (len == 0) continue;

    line[len] = '\0';
    len = 0;

    const char tag = (char)tolower(line[0]);
    const int8_t source = (tag == 'a') ? SRC_A : ((tag == 'b') ? SRC_B : -1);
    char *cursor = &line[1];
    const long byteIndex = strtol(cursor, &cursor, 10);
    const long value = strtol(cursor, &cursor, 0);

    if (source < 0 || byteIndex < 0 || byteIndex > 7 || value < 0 || value > 255) {
      Serial.println(F("usage: a<0-7> <value>   e.g. \"a0 1\""));
      continue;
    }

    aggInjectByte((uint8_t)source, (uint8_t)byteIndex, (uint8_t)value, millis());
    g_injecting = true;
  }
}
#endif

void setup() {
#if DEBUG_SERIAL
  Serial.begin(SERIAL_BAUD);
  Serial.println(F("Batmobile CAN input aggregator"));
#endif

  aggReset();

#ifdef SS
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
#endif

#if USE_HARDWARE_FILTERS
  const uint8_t idMode = MCP_STDEXT;
#else
  const uint8_t idMode = MCP_ANY;
#endif

#if MCP_CRYSTAL_AUTO
  const uint8_t crystal = detectCrystal();
#else
  const uint8_t crystal = MCP_CRYSTAL;
#endif

  while (CAN0.begin(idMode, CAN_250KBPS, crystal) != CAN_OK) {
#if DEBUG_SERIAL
    Serial.println(F("MCP2515 init failed - check CS pin and crystal setting"));
#endif
    delay(500);
  }

#if USE_HARDWARE_FILTERS
  const uint32_t pgnMask = 0x03FFFF00UL;
  CAN0.init_Mask(0, 1, pgnMask);
  CAN0.init_Mask(1, 1, pgnMask);
  CAN0.init_Filt(0, 1, SRC_PGN_A << 8);
  CAN0.init_Filt(1, 1, SRC_PGN_B << 8);
  CAN0.init_Filt(2, 1, SRC_PGN_A << 8);
  CAN0.init_Filt(3, 1, SRC_PGN_B << 8);
  CAN0.init_Filt(4, 1, SRC_PGN_A << 8);
  CAN0.init_Filt(5, 1, SRC_PGN_B << 8);
#endif

  CAN0.setMode(MCP_NORMAL);
  g_nextSendMs = millis();
}

void loop() {
  pollReceive();
#if DEBUG_SERIAL
  pollSerialInjector();
#endif

  const uint32_t now = millis();

#if DEBUG_SERIAL
  if (g_injecting) {
    for (uint8_t s = 0; s < SRC_COUNT; s++) aggTouchSource(s, now);
  }

  const uint8_t expired = aggExpireStaleSources(now);
  for (uint8_t s = 0; s < SRC_COUNT; s++) {
    if (!(expired & (1u << s))) continue;
    Serial.print(F("[timeout] source "));
    Serial.write(s == SRC_A ? 'A' : 'B');
    Serial.println(F(" went quiet, its inputs cleared"));
  }
#else
  aggExpireStaleSources(now);
#endif

  if ((int32_t)(now - g_nextSendMs) >= 0) {
    g_nextSendMs += OUT_PERIOD_MS;
    if ((int32_t)(now - g_nextSendMs) >= 0) g_nextSendMs = now + OUT_PERIOD_MS;
    sendAggregatedFrame();
  }
}
