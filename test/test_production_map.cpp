// Tests the real mapping table from input_map.h - the one that will actually
// run on the car - rather than a table written for the tests. Compiled without
// AGG_TEST_MAP so it picks up the production rows.

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

// The frame the Mastercell should see when the listed inputs, and only those,
// are active. Inputs are given as IN numbers.
static void expectInputs(const char *what, const uint8_t *inputs, int count) {
  uint8_t expected[8] = {0};
  for (int i = 0; i < count; i++) {
    const uint8_t index = (uint8_t)(inputs[i] - 1);
    expected[index >> 3] |= (uint8_t)(0x80u >> (index & 7));
  }
  expected[6] = 0xA5;
  expectPayload(what, expected);
}

static void expectNothing(const char *what) { expectInputs(what, NULL, 0); }

static void expectOnly(const char *what, uint8_t input) {
  expectInputs(what, &input, 1);
}

// ---------------------------------------------------------------------------
// Panel state
// ---------------------------------------------------------------------------

// Byte order of the AiM SW4 switch frame.
enum { B_LOW_BEAM = 0, B_HIGH_BEAM = 1, B_HORN = 2, B_EXH_LOUD = 3,
       B_EXH_QUIET = 4, B_HAZARDS = 5 };

static uint8_t g_switches[8];

static void setSwitch(int byteIndex, bool on) {
  g_switches[byteIndex] = on ? 1 : 0;
  aggHandleFrame(0x18EF011EUL, g_switches, 8, 1000);
}

static void allSwitchesOff() {
  memset(g_switches, 0, sizeof(g_switches));
  aggHandleFrame(0x18EF011EUL, g_switches, 8, 1000);
}

// Rotary detent as the panel encodes it, matching POS() in input_map.h.
static uint16_t pos(uint8_t position) {
#if ROTARY_AS_BITMASK
  return (uint16_t)(1u << (position - 1));
#else
  return position;
#endif
}

// Every knob rests at position 1.
static void rotaries(uint8_t left, uint8_t middle, uint8_t right) {
  const uint16_t l = pos(left), m = pos(middle), r = pos(right);
  const uint8_t payload[8] = {
      (uint8_t)(l & 0xFF), (uint8_t)(l >> 8), (uint8_t)(m & 0xFF),
      (uint8_t)(m >> 8),   (uint8_t)(r & 0xFF), (uint8_t)(r >> 8),
      0,                   0};
  aggHandleFrame(0x18EF021EUL, payload, 8, 1000);
}

static void resetPanels() {
  aggReset();
  allSwitchesOff();
  rotaries(1, 1, 1);
}

// ---------------------------------------------------------------------------
// Named inputs, so a renumbering shows up as one edit here.
// ---------------------------------------------------------------------------

enum {
  IN_EXHAUST_LOUD = 17,
  IN_EXHAUST_QUIET = 18,
  IN_HORN = 19,
  IN_HAZARDS = 20,
  IN_LOW_BEAMS = 21,
  IN_HIGH_BEAMS = 22,
  IN_WIPERS_LOW = 23,
  IN_WIPERS_HIGH = 24,
  IN_SMOKE_POWER = 25,
  IN_INTERIOR_LIGHTS = 26,
  IN_ENGINE_LIGHTS = 27,
};

// ---------------------------------------------------------------------------

// Each switch drives its own input and nothing else.
static void testEachSwitchAlone() {
  struct Case {
    const char *name;
    int byteIndex;
    uint8_t input;
  };

  const Case cases[] = {
      {"low beams", B_LOW_BEAM, IN_LOW_BEAMS},
      {"high beams", B_HIGH_BEAM, IN_HIGH_BEAMS},
      {"horn", B_HORN, IN_HORN},
      {"exhaust loud", B_EXH_LOUD, IN_EXHAUST_LOUD},
      {"exhaust quiet", B_EXH_QUIET, IN_EXHAUST_QUIET},
      {"hazards", B_HAZARDS, IN_HAZARDS},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    resetPanels();
    setSwitch(cases[i].byteIndex, true);
    char label[64];
    snprintf(label, sizeof(label), "%s alone", cases[i].name);
    expectOnly(label, cases[i].input);
  }
}

// The point of the whole exercise: switches accumulate rather than replace.
static void testSwitchesAggregate() {
  resetPanels();

  setSwitch(B_LOW_BEAM, true);
  expectOnly("low beams on", IN_LOW_BEAMS);

  setSwitch(B_HORN, true);
  const uint8_t lowAndHorn[] = {IN_LOW_BEAMS, IN_HORN};
  expectInputs("horn pressed, low beams hold", lowAndHorn, 2);

  setSwitch(B_HAZARDS, true);
  const uint8_t three[] = {IN_LOW_BEAMS, IN_HORN, IN_HAZARDS};
  expectInputs("hazards on as well", three, 3);

  setSwitch(B_HORN, false);
  const uint8_t lowAndHazards[] = {IN_LOW_BEAMS, IN_HAZARDS};
  expectInputs("horn released, the other two hold", lowAndHazards, 2);

  const uint8_t all[] = {IN_LOW_BEAMS, IN_HIGH_BEAMS, IN_HORN,
                         IN_HAZARDS,   IN_EXHAUST_LOUD, IN_EXHAUST_QUIET};
  setSwitch(B_HIGH_BEAM, true);
  setSwitch(B_HORN, true);
  setSwitch(B_EXH_LOUD, true);
  setSwitch(B_EXH_QUIET, true);
  expectInputs("all six switches on", all, 6);

  allSwitchesOff();
  expectNothing("all six off");
}

// Wipers: position 1 off, 3 low, 5 high.
static void testWiperRotary() {
  resetPanels();

  expectNothing("wipers at rest position 1");

  rotaries(1, 1, 3);
  expectOnly("wipers position 3 -> low speed", IN_WIPERS_LOW);

  rotaries(1, 1, 5);
  expectOnly("wipers position 5 -> high speed, low released", IN_WIPERS_HIGH);

  rotaries(1, 1, 1);
  expectNothing("wipers back to position 1, both released");
}

// Smoke machine main power on the middle knob, position 5.
static void testSmokeRotary() {
  resetPanels();

  rotaries(1, 5, 1);
  expectOnly("middle position 5 -> smoke machine power", IN_SMOKE_POWER);

  rotaries(1, 1, 1);
  expectNothing("middle back to position 1 -> smoke machine power off");
}

// The interior lighting requirement, stated explicitly: position 3 lights the
// footwells and centre tunnel and must NOT light the engine bay or jet nozzle,
// and position 7 does the reverse.
static void testInteriorLightingIsExclusive() {
  resetPanels();

  rotaries(3, 1, 1);
  expectOnly("left position 3 -> interior only, not engine", IN_INTERIOR_LIGHTS);

  rotaries(7, 1, 1);
  expectOnly("left position 7 -> engine only, not interior", IN_ENGINE_LIGHTS);

  rotaries(3, 1, 1);
  expectOnly("back to position 3 -> interior only again", IN_INTERIOR_LIGHTS);

  rotaries(1, 1, 1);
  expectNothing("left position 1 -> all interior lighting off");
}

// Unused detents on each knob must not assert anything.
static void testUnusedDetents() {
  resetPanels();

  rotaries(2, 1, 1);
  expectNothing("left position 2 is unmapped");
  rotaries(1, 3, 1);
  expectNothing("middle position 3 is unmapped");
  rotaries(1, 1, 7);
  expectNothing("right position 7 is unmapped");
}

// A knob showing two detents at once, as it might mid-turn, asserts neither
// rather than both. This is what keeps the lighting outputs exclusive.
static void testMidTurnAssertsNeither() {
#if ROTARY_AS_BITMASK
  resetPanels();

  const uint16_t between = (uint16_t)(pos(3) | pos(7));
  const uint8_t payload[8] = {(uint8_t)(between & 0xFF), (uint8_t)(between >> 8),
                              1, 0, 1, 0, 0, 0};
  aggHandleFrame(0x18EF021EUL, payload, 8, 1000);

  expectNothing("left knob showing positions 3 and 7 at once asserts neither");
#endif
}

// Switches and rotaries share the frame without treading on each other.
static void testSwitchesAndRotariesCoexist() {
  resetPanels();
  setSwitch(B_LOW_BEAM, true);
  setSwitch(B_HORN, true);
  rotaries(7, 1, 3);

  const uint8_t expected[] = {IN_LOW_BEAMS, IN_HORN, IN_ENGINE_LIGHTS,
                              IN_WIPERS_LOW};
  expectInputs("low beams + horn + engine lights + wipers low", expected, 4);
}

// IN01-IN16 belong to the physical rocker switches and must stay clear.
static void testFirstTwoBytesNeverUsed() {
  resetPanels();
  setSwitch(B_LOW_BEAM, true);
  setSwitch(B_HIGH_BEAM, true);
  setSwitch(B_HORN, true);
  setSwitch(B_EXH_LOUD, true);
  setSwitch(B_EXH_QUIET, true);
  setSwitch(B_HAZARDS, true);
  rotaries(7, 5, 5);

  uint8_t actual[8];
  aggBuildPayload(actual);

  g_checks++;
  if (actual[0] != 0 || actual[1] != 0) {
    g_failures++;
    printf("FAIL  everything on still leaves IN01-IN16 clear\n");
    printf("        byte 0 = %02X, byte 1 = %02X, both should be 00\n", actual[0],
           actual[1]);
  }
}

// Losing the switch panel must not take the rotaries down with it.
static void testPanelsExpireIndependently() {
  resetPanels();
  setSwitch(B_LOW_BEAM, true);
  rotaries(1, 1, 3);

  // The rotary panel keeps talking while the switch panel goes quiet.
  const uint16_t r = pos(3);
  const uint8_t stillTalking[8] = {
      (uint8_t)(pos(1) & 0xFF), (uint8_t)(pos(1) >> 8), (uint8_t)(pos(1) & 0xFF),
      (uint8_t)(pos(1) >> 8),   (uint8_t)(r & 0xFF),    (uint8_t)(r >> 8),
      0,                        0};
  aggHandleFrame(0x18EF021EUL, stillTalking, 8, 1000 + SRC_TIMEOUT_MS);
  aggExpireStaleSources(1000 + SRC_TIMEOUT_MS);

  expectOnly("switch panel expired, wipers keep running", IN_WIPERS_LOW);
}

// ---------------------------------------------------------------------------

int main() {
  testEachSwitchAlone();
  testSwitchesAggregate();
  testWiperRotary();
  testSmokeRotary();
  testInteriorLightingIsExclusive();
  testUnusedDetents();
  testMidTurnAssertsNeither();
  testSwitchesAndRotariesCoexist();
  testFirstTwoBytesNeverUsed();
  testPanelsExpireIndependently();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
