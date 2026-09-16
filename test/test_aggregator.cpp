// Host-side tests for the aggregation logic. Build and run with test/run.sh.

#include <stdio.h>
#include <string.h>

#include "aggregator.h"

static int g_failures = 0;
static int g_checks = 0;

static void expectPayload(const char *what, const uint8_t expected[8]) {
  uint8_t actual[8];
  aggBuildPayload(actual);
  g_checks++;

  if (memcmp(actual, expected, 8) == 0) return;

  g_failures++;
  printf("FAIL  %s\n        expected", what);
  for (int i = 0; i < 8; i++) printf(" %02X", expected[i]);
  printf("\n        actual  ");
  for (int i = 0; i < 8; i++) printf(" %02X", actual[i]);
  printf("\n");
}

static void expectEqual(const char *what, unsigned long expected, unsigned long actual) {
  g_checks++;
  if (expected == actual) return;
  g_failures++;
  printf("FAIL  %s: expected 0x%lX, got 0x%lX\n", what, expected, actual);
}

// Builds an expected payload from a list of (byte, mask) pairs, always with
// the guard byte in place.
struct BitSpec {
  uint8_t byteIndex;
  uint8_t mask;
};

static void buildExpected(uint8_t out[8], const BitSpec *bits, int count) {
  memset(out, 0, 8);
  for (int i = 0; i < count; i++) out[bits[i].byteIndex] |= bits[i].mask;
  out[6] = 0xA5;
  out[7] = 0x00;
}

static void sendSource(uint8_t source, const uint8_t payload[8], uint32_t now) {
  const uint32_t id = (source == SRC_A) ? 0x18EF011EUL : 0x18EF021EUL;
  aggHandleFrame(id, payload, 8, now);
}

// ---------------------------------------------------------------------------

// Every input index must land where the Mastercell spec says it does.
static void testBitLayoutMatchesSpec() {
  struct Case {
    const char *name;
    uint8_t index;
    uint8_t byteIndex;
    uint8_t mask;
  };

  const Case cases[] = {
      {"IN01", IN(1), 0, 0x80},   {"IN08", IN(8), 0, 0x01},
      {"IN09", IN(9), 1, 0x80},   {"IN16", IN(16), 1, 0x01},
      {"IN17", IN(17), 2, 0x80},  {"IN24", IN(24), 2, 0x01},
      {"IN25", IN(25), 3, 0x80},  {"IN32", IN(32), 3, 0x01},
      {"IN33", IN(33), 4, 0x80},  {"IN38", IN(38), 4, 0x04},
      {"HSIN01", HSIN(1), 4, 0x02}, {"HSIN02", HSIN(2), 4, 0x01},
      {"HSIN03", HSIN(3), 5, 0x80}, {"HSIN06", HSIN(6), 5, 0x10},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const Case &c = cases[i];
    char label[64];
    snprintf(label, sizeof(label), "%s byte index", c.name);
    expectEqual(label, c.byteIndex, (unsigned long)(c.index >> 3));
    snprintf(label, sizeof(label), "%s bit mask", c.name);
    expectEqual(label, c.mask, (unsigned long)(0x80u >> (c.index & 7)));
  }
}

// The two worked examples from the Mastercell messaging spec.
static void testSpecWorkedExamples() {
  aggReset();
  uint8_t frame[8] = {0};
  uint8_t expected[8];

  frame[0] = 1;  // IN01 on
  sendSource(SRC_A, frame, 1000);
  const BitSpec in01[] = {{0, 0x80}};
  buildExpected(expected, in01, 1);
  expectPayload("IN01 alone -> 80 00 00 00 00 00 A5 00", expected);

  frame[1] = 1;  // IN02 on as well
  sendSource(SRC_A, frame, 1000);
  const BitSpec in01in02[] = {{0, 0xC0}};
  buildExpected(expected, in01in02, 1);
  expectPayload("IN01 + IN02 -> C0 00 00 00 00 00 A5 00", expected);

  frame[1] = 0;  // IN02 back off, IN01 must hold
  sendSource(SRC_A, frame, 1000);
  buildExpected(expected, in01, 1);
  expectPayload("IN02 released, IN01 holds -> 80 ...", expected);
}

// Inputs from both panels have to coexist in one frame.
static void testAggregatesAcrossBothSources() {
  aggReset();
  uint8_t a[8] = {0};
  uint8_t b[8] = {0};

  a[0] = 1;  // IN01
  a[2] = 1;  // IN09
  b[0] = 1;  // IN17
  b[1] = 1;  // IN32
  sendSource(SRC_A, a, 1000);
  sendSource(SRC_B, b, 1000);

  const BitSpec bits[] = {{0, 0x80}, {1, 0x80}, {2, 0x80}, {3, 0x01}};
  uint8_t expected[8];
  buildExpected(expected, bits, 4);
  expectPayload("IN01 + IN09 + IN17 + IN32 across both panels", expected);
}

// Byte 4 mixes IN33-IN38 with HSIN01/HSIN02; byte 5's top nibble is reserved.
static void testHighSideAndReservedBits() {
  aggReset();
  uint8_t a[8] = {0};

  a[3] = 1;  // IN38   -> byte 4, 0x04
  a[4] = 1;  // HSIN01 -> byte 4, 0x02
  a[5] = 1;  // HSIN02 -> byte 4, 0x01
  a[6] = 1;  // HSIN03 -> byte 5, 0x80
  a[7] = 1;  // HSIN06 -> byte 5, 0x10
  sendSource(SRC_A, a, 1000);

  const BitSpec bits[] = {{4, 0x07}, {5, 0x90}};
  uint8_t expected[8];
  buildExpected(expected, bits, 2);
  expectPayload("IN38 + HSIN01-03 + HSIN06, reserved nibble clear", expected);

  uint8_t actual[8];
  aggBuildPayload(actual);
  expectEqual("byte 5 reserved bits stay zero", 0, (unsigned long)(actual[5] & 0x0F));
  expectEqual("guard byte", 0xA5, actual[6]);
  expectEqual("byte 7 reserved", 0x00, actual[7]);
}

// A MATCH_EQUAL knob reports its detent as a plain number, so only one of its
// inputs is ever set.
static void testKnobDetents() {
  aggReset();
  uint8_t b[8] = {0};
  uint8_t expected[8];

  b[3] = 2;  // detent 2 -> IN21 -> byte 2, 0x08
  sendSource(SRC_B, b, 1000);
  const BitSpec detent2[] = {{2, 0x08}};
  buildExpected(expected, detent2, 1);
  expectPayload("knob detent 2 sets IN21 only", expected);

  b[3] = 3;  // detent 3 -> IN22 -> byte 2, 0x04
  sendSource(SRC_B, b, 1000);
  const BitSpec detent3[] = {{2, 0x04}};
  buildExpected(expected, detent3, 1);
  expectPayload("knob detent 3 clears IN21 and sets IN22", expected);

  b[3] = 0;  // knob to a position with no mapping
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, NULL, 0);
  expectPayload("knob detent 0 clears all three", expected);
}

// A rotary spanning two bytes is read as one little-endian 16-bit bitmask.
static void testTwoByteRotary() {
  aggReset();
  uint8_t b[8] = {0};
  uint8_t expected[8];

  b[6] = 0x01;  // -> IN26 -> byte 3, 0x40
  b[7] = 0x00;
  sendSource(SRC_B, b, 1000);
  const BitSpec pos1[] = {{3, 0x40}};
  buildExpected(expected, pos1, 1);
  expectPayload("rotary bit 0 sets IN26", expected);

  b[6] = 0x02;  // -> IN27 -> byte 3, 0x20
  sendSource(SRC_B, b, 1000);
  const BitSpec pos2[] = {{3, 0x20}};
  buildExpected(expected, pos2, 1);
  expectPayload("rotary bit 1 sets IN27", expected);

  // Bit 8 lives in the second byte of the pair, so a swapped byte order or an
  // ignored high byte would both fail here.
  b[6] = 0x00;
  b[7] = 0x01;  // -> IN28 -> byte 3, 0x10
  sendSource(SRC_B, b, 1000);
  const BitSpec pos256[] = {{3, 0x10}};
  buildExpected(expected, pos256, 1);
  expectPayload("rotary bit 8 sets IN28, proving little-endian", expected);

  // Two bits at once, as a knob may report mid-detent, sets both inputs.
  b[6] = 0x03;
  b[7] = 0x00;
  sendSource(SRC_B, b, 1000);
  const BitSpec both[] = {{3, 0x60}};
  buildExpected(expected, both, 1);
  expectPayload("rotary bits 0 and 1 together set IN26 and IN27", expected);

  // A bit with no mapped position leaves all three clear.
  b[6] = 0x40;
  b[7] = 0x00;
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, NULL, 0);
  expectPayload("unmapped rotary bit sets nothing", expected);
}

// A momentary button in MODE_TOGGLE latches on the press, not the release.
static void testToggleMode() {
  aggReset();
  uint8_t b[8] = {0};
  uint8_t expected[8];
  const BitSpec in25[] = {{3, 0x80}};

  b[4] = 1;  // press
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, in25, 1);
  expectPayload("toggle: first press latches IN25 on", expected);

  b[4] = 0;  // release, latch must hold
  sendSource(SRC_B, b, 1000);
  expectPayload("toggle: release holds IN25 on", expected);

  b[4] = 1;  // second press
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, NULL, 0);
  expectPayload("toggle: second press latches IN25 off", expected);

  b[4] = 0;
  sendSource(SRC_B, b, 1000);
  expectPayload("toggle: release holds IN25 off", expected);
}

// Two controls driving one input: it stays high until both are released.
static void testSharedInputIsOred() {
  aggReset();
  uint8_t a[8] = {0};
  uint8_t b[8] = {0};
  uint8_t expected[8];
  const BitSpec in02[] = {{0, 0x40}};

  a[1] = 1;  // panel A drives IN02
  b[5] = 1;  // panel B drives IN02 too
  sendSource(SRC_A, a, 1000);
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, in02, 1);
  expectPayload("shared IN02 high from both panels", expected);

  a[1] = 0;
  sendSource(SRC_A, a, 1000);
  expectPayload("shared IN02 still high from panel B alone", expected);

  b[5] = 0;
  sendSource(SRC_B, b, 1000);
  buildExpected(expected, NULL, 0);
  expectPayload("shared IN02 low once both released", expected);
}

// A panel that stops transmitting must not leave its inputs latched on.
static void testSourceTimeout() {
  aggReset();
  uint8_t a[8] = {0};
  uint8_t b[8] = {0};
  uint8_t expected[8];

  a[0] = 1;  // IN01 from panel A
  b[0] = 1;  // IN17 from panel B
  sendSource(SRC_A, a, 1000);
  sendSource(SRC_B, b, 1000);

  // Panel B keeps talking, panel A goes quiet.
  sendSource(SRC_B, b, 1000 + SRC_TIMEOUT_MS);
  const uint8_t expiredMask = aggExpireStaleSources(1000 + SRC_TIMEOUT_MS);

  expectEqual("only panel A expired", 1u << SRC_A, expiredMask);
  const BitSpec in17[] = {{2, 0x80}};
  buildExpected(expected, in17, 1);
  expectPayload("expired panel A drops IN01, panel B keeps IN17", expected);
}

// Nothing heard yet still has to produce a valid heartbeat frame.
static void testHeartbeatBeforeAnyTraffic() {
  aggReset();
  uint8_t expected[8];
  buildExpected(expected, NULL, 0);
  expectPayload("idle heartbeat is all zeros plus the guard byte", expected);
}

// Priority and source address must not affect matching.
static void testMatchingIgnoresPriorityAndAddress() {
  expectEqual("0x18EF011E -> key 0xEF01", 0xEF01, aggMatchKeyFromId(0x18EF011EUL));
  expectEqual("0x0CEF0142 -> key 0xEF01", 0xEF01, aggMatchKeyFromId(0x0CEF0142UL));
  expectEqual("0x18CF0080 -> key 0xCF00", 0xCF00, aggMatchKeyFromId(0x18CF0080UL));

  aggReset();
  uint8_t a[8] = {0};
  a[0] = 1;
  // Same PF/PS, wildly different priority and source address.
  expectEqual("frame accepted regardless of priority/SA", 1,
              aggHandleFrame(0x00EF01FFUL, a, 8, 1000));

  uint8_t expected[8];
  const BitSpec in01[] = {{0, 0x80}};
  buildExpected(expected, in01, 1);
  expectPayload("IN01 set from an odd-priority frame", expected);

  expectEqual("unrelated PGN rejected", 0, aggHandleFrame(0x18FEE500UL, a, 8, 1000));
}

// The two panels are PDU1 and share PGN 0xEF00, differing only in the
// destination byte. Masking that byte off would merge them into one source.
static void testPdu1PanelsAreNotConflated() {
  expectEqual("panel A key", 0xEF01, aggMatchKeyFromId(0x18EF011EUL));
  expectEqual("panel B key", 0xEF02, aggMatchKeyFromId(0x18EF021EUL));

  aggReset();
  uint8_t payload[8] = {0};
  payload[0] = 1;

  // Byte 0 means IN01 on panel A but IN17 on panel B. If the destination byte
  // were masked off, the second frame would overwrite the first source's data
  // and only one of the two inputs would ever be set.
  aggHandleFrame(0x18EF011EUL, payload, 8, 1000);
  aggHandleFrame(0x18EF021EUL, payload, 8, 1000);

  const BitSpec bits[] = {{0, 0x80}, {2, 0x80}};
  uint8_t expected[8];
  buildExpected(expected, bits, 2);
  expectPayload("identical payloads on both panels set IN01 and IN17", expected);

  // A third destination on the same PGN is not one of ours.
  expectEqual("PGN 0xEF00 to an unmapped destination is rejected", 0,
              aggHandleFrame(0x18EF031EUL, payload, 8, 1000));
}

// Short frames must zero-fill rather than reuse the previous payload.
static void testShortFrameZeroFills() {
  aggReset();
  uint8_t a[8] = {0};
  a[0] = 1;
  a[1] = 1;
  sendSource(SRC_A, a, 1000);

  uint8_t shortFrame[2] = {1, 0};
  aggHandleFrame(0x18EF011EUL, shortFrame, 2, 1000);

  uint8_t expected[8];
  const BitSpec in01[] = {{0, 0x80}};
  buildExpected(expected, in01, 1);
  expectPayload("2-byte frame clears the untransmitted bytes", expected);
}

// ---------------------------------------------------------------------------

int main() {
  testBitLayoutMatchesSpec();
  testSpecWorkedExamples();
  testAggregatesAcrossBothSources();
  testHighSideAndReservedBits();
  testKnobDetents();
  testTwoByteRotary();
  testToggleMode();
  testSharedInputIsOred();
  testSourceTimeout();
  testHeartbeatBeforeAnyTraffic();
  testMatchingIgnoresPriorityAndAddress();
  testPdu1PanelsAreNotConflated();
  testShortFrameZeroFills();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
