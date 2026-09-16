# Batmobile CAN Input Aggregator

An Arduino bridge between two J1939 button/knob panels and an Infinitybox
Mastercell. It watches the two panel frames, `0x18EF011E` and `0x18EF021E`,
holds the combined state of every control, and republishes it as one aggregated
input frame on `0x18CF0080` every 250 ms.

The behaviour the Mastercell requires is that each control only ever changes
its own bit while every other active input keeps its high state. That falls out
of the design here: the Arduino owns the full picture of what is on, and
rebuilds the whole frame from scratch before each transmission.

## Layout

| Path | What it is |
| --- | --- |
| `BatmobileInputAggregator/BatmobileInputAggregator.ino` | Hardware layer: MCP2515 setup, receive polling, 250 ms transmit timer, serial console |
| `BatmobileInputAggregator/input_map.h` | Everything you are meant to edit: pins, crystal, panel ids, and the button-to-input mapping table |
| `BatmobileInputAggregator/aggregator.cpp` | The state machine. No CAN, no Serial, no `millis()`, so it can be tested on a PC |
| `test/` | Host-side tests for the aggregation logic |

## Hardware

- Arduino Mega 2560 R3
- Seeed Studio CAN-BUS shield — MCP2515 controller, MCP2551 transceiver
- J1939 bus at 250 kbps, 29-bit extended ids
- 120 Ω termination at both ends of the bus, as usual

Three things about this combination are worth knowing, because each of them
produces the same unhelpful symptom — a board that boots fine and never
receives anything.

**Chip select is D9, not D10.** Every Seeed shield from V1.1 onwards puts CS on
D9, while most MCP2515 example code you will find online assumes D10. The
shield can be converted to D10 by cutting and re-soldering the CS pads on its
underside; if yours has been, set `MCP_CS_PIN` to 10.

**SPI reaches the shield through the ICSP header.** The Mega has its SPI pins
on D50–D52 rather than the D11–D13 that a Uno-shaped shield sits on, so a
shield that only wired SPI to D11–D13 would need jumpers. Seeed routes SPI to
the ICSP header by default, which is exactly what makes the shield Mega-
compatible, so no modification is needed. Only the original V1.0 shield lacks
this. If yours has three pads on the back labelled MOSI, MISO and SCK, they
must be on the `A` side.

**The Mega's hardware slave-select pin, D53, must be an output.** On AVR, if
that pin is left as a floating input and something pulls it low, the SPI
peripheral silently demotes itself from master to slave and the shield stops
answering. The shield does not connect D53, so `setup()` drives it high. This
is the usual cause of a Mega and CAN shield that work one day and not the next.

The crystal is 16 MHz on this shield, so `MCP_CRYSTAL_AUTO` is off and
`MCP_CRYSTAL` is set directly. The auto-detect is still there if you move to
unknown hardware: it listens at 16 MHz and then 8 MHz and keeps whichever rate
hears real traffic, probing in listen-only mode so a node at the wrong bit rate
cannot spray error frames over everyone else. It needs the panels transmitting,
and costs up to `CRYSTAL_PROBE_MS` per candidate before the first heartbeat
goes out, which is why it is not on by default.

## Setup

Install the `mcp_can` library by coryjfowler, either from the Arduino IDE
library manager or with:

```bash
arduino-cli lib install mcp_can
arduino-cli core install arduino:avr
```

Then compile and upload. Find your port with `arduino-cli board list`:

```bash
arduino-cli compile --fqbn arduino:avr:mega BatmobileInputAggregator
arduino-cli upload  --fqbn arduino:avr:mega -p /dev/cu.usbmodem1101 BatmobileInputAggregator
```

The sketch is about 8.5 KB of the Mega's 254 KB and 281 bytes of its 8 KB RAM,
so there is plenty of room to grow.

## The transmitted frame

`0x18CF0080` is priority 6, PGN `0xCF00`, source address `0x80`. The Mastercell
matches on PGN only, so priority and source address are free.

| Byte | Contents |
| --- | --- |
| 0 | IN01–IN08 |
| 1 | IN09–IN16 |
| 2 | IN17–IN24 |
| 3 | IN25–IN32 |
| 4 | IN33–IN38 in bits 0–5, HSIN01 in bit 6, HSIN02 in bit 7 |
| 5 | HSIN03–HSIN06 in bits 0–3, bits 4–7 reserved |
| 6 | Guard byte, always `0xA5` |
| 7 | Reserved, always `0x00` |

Bit 0 of each byte is the most significant bit, so IN01 is `0x80`. IN01 alone
sends `80 00 00 00 00 00 A5 00`; IN01 plus IN02 sends `C0 00 00 00 00 00 A5 00`.

Worth knowing: the whole range is linear once you number the inputs IN01–IN38
as 0–37 and HSIN01–HSIN06 as 38–43. Every one of them lands at
`byte = index / 8` with `mask = 0x80 >> (index % 8)`, including across the
IN38/HSIN01 boundary in byte 4. That is why the encoder has no special cases.

## Input allocation

The billet rocker switches — doors, smoke screen deploy, side flaps and roof
wings — are wired straight into the Mastercell's physical input terminals and
never touch this Arduino. Counting each direction separately they need nine
inputs, and they will almost certainly be wired to the low-numbered terminals.

The CAN controls therefore start at **IN17**, leaving IN01–IN16 clear. Nothing
this sketch sends can ever set a bit in bytes 0 or 1 of the frame, and there is
a test that asserts exactly that with every control switched on at once.

This matters because a CAN controlled input and a physical input wire with the
same number are the same logical input to the Mastercell. If they overlapped, a
button on the keypad could open a door. Worth confirming with Infinitybox that
the two share a numbering space — the allocation above is safe either way, so
it costs nothing to assume they do.

## Current control assignments

Switch panel, `0x18EF011E`, one byte per switch:

| Byte | Control | Input |
| --- | --- | --- |
| 0 | Low beams | IN21 |
| 1 | High beams | IN22 |
| 2 | Horn | IN19 |
| 3 | Exhaust loud | IN17 |
| 4 | Exhaust quiet | IN18 |
| 5 | Hazards | IN20 |
| 6–7 | unused | — |

Rotary panel, `0x18EF021E`, two bytes per knob. Position 1 is the rest position
on all three and drives nothing:

| Bytes | Knob | Position | Function | Input |
| --- | --- | --- | --- | --- |
| 0–1 | Left | 3 | Footwell + centre tunnel ambient | IN26 |
| 0–1 | Left | 7 | Engine ambient + jet nozzle | IN27 |
| 2–3 | Middle | 5 | Smoke machine main power | IN25 |
| 4–5 | Right | 3 | Wipers low | IN23 |
| 4–5 | Right | 5 | Wipers high | IN24 |
| 6–7 | — | — | unused | — |

Two things here are assumptions rather than things you told me:

- **Which switch is on which byte.** Low beams, high beams, horn and hazards
  are on bytes 0, 1, 2 and 5 as originally given. Exhaust loud and quiet are on
  bytes 3 and 4, the two previously described as show lights and aux switch.
- **The input numbers themselves.** They were allocated in a block from IN17;
  change them to whatever the inCODE NGX cases expect.

## Momentary or latching

You mentioned you can configure the SW4 switches either way. The recommendation
is to make everything except the horn latch **on the keypad**, and leave the
sketch in `MODE_FOLLOW` so it simply mirrors what the panel reports.

Keeping the state on the keypad means one source of truth. The keypad's own
LEDs stay honest, and nothing gets out of step if the Arduino resets or the
panel drops off the bus for a moment. If the Arduino held the latches instead,
a reset would silently turn your headlights off while the keypad still showed
them on.

The horn wants to stay momentary, held high only while pressed, which
`MODE_FOLLOW` already gives you.

If you would rather latch a particular control here, change its row to
`MODE_TOGGLE` and each press of a momentary button will flip the input.

## How the rotaries are read

Rotary rows are written with the position numbers marked on the switch —
`POS(3)`, `POS(5)`, `POS(7)` — so the table reads the same as your notes. The
`POS()` macro turns those into whatever the panel actually puts on the wire.

With `ROTARY_AS_BITMASK` set, which is the default, position *n* is bit *n−1*
of the 16-bit field, so position 3 is `0x0004` and position 7 is `0x0040`.
Little-endian is the J1939 convention for multi-byte fields, so bit 0 lives in
the lower-numbered byte of the pair and bit 8 in the higher one. Sixteen bits
covers a sixteen-position knob, which is presumably why each one gets two
bytes.

If the SW4 turns out to send the plain detent number instead — `3` for position
3 — set `ROTARY_AS_BITMASK` to 0. Nothing else changes. The two encodings are
easy to tell apart on a bus analyser: watch a knob go to position 3 and see
whether the field reads `0x0004` or `0x0003`.

### Why the rotaries match exactly

Rotary rows use `MATCH_EQUAL`, so a detent asserts its input only when the
field holds that position and nothing else. This is deliberate, and it is what
delivers your "only one output per position" requirement.

The alternative, testing with a bitwise AND, looks more natural for a bitmask
but breaks the requirement. If the left knob momentarily reports positions 3
and 7 together while turning past a detent, a bitwise match lights the interior
lights *and* the engine bay and jet nozzle at the same instant — the exact
combination you asked to avoid. Matching exactly means that transient asserts
neither, and the correct single output appears once the knob settles.

There is a test for this, and it fails if the rows are changed to a bitwise
match.

## Editing the mapping

The table at the bottom of `input_map.h` is the only thing you need to touch to
re-assign controls. One row per control, or one row per position for a knob:

```c
{SRC_A, 0, 1, MATCH_BITS, ANY_NONZERO, IN(21), MODE_FOLLOW},
{SRC_B, 4, 2, MATCH_EQUAL, POS(3), IN(23), MODE_FOLLOW},
```

The first reads: when byte 0 of the switch panel is non-zero, drive IN21 high.
The second: when bytes 4–5 of the rotary panel hold position 3 exactly, drive
IN23 high.

- **Source** is `SRC_A` or `SRC_B`, the two panels.
- **Byte index** is 0–7, the first byte of the field.
- **Width** is 1 or 2 bytes. Two-byte fields are read little-endian.
- **Match** is `MATCH_BITS` to test the value against a bit mask, or
  `MATCH_EQUAL` to require an exact value.
- **Match value** is the mask or the exact value. `ANY_NONZERO` with
  `MATCH_BITS` means any non-zero value, which is what a plain on/off switch
  wants. `POS(n)` gives the encoding for rotary detent *n*.
- **Target** is `IN(1)`–`IN(38)` or `HSIN(1)`–`HSIN(6)`.
- **Mode** is `MODE_FOLLOW` when the panel holds the value for as long as the
  control is active, or `MODE_TOGGLE` for a momentary button that should latch
  on and off here in the Arduino instead.

Two rows may target the same input; it goes high when either control is active.
Rows are independent, so nothing you add can disturb another input's bit.

## Fail-safe

If a panel stops transmitting for `SRC_TIMEOUT_MS` (1 s by default), every
input fed by that panel is driven back to zero rather than latching on
indefinitely. The other panel is unaffected. Set `SRC_TIMEOUT_MS` to 0 to
disable this and hold the last known state instead.

The 250 ms frame is unconditional, so the Mastercell sees a steady heartbeat
even when nothing is pressed and even before either panel has said anything.

## Bench testing without the panels

With `DEBUG_SERIAL` on, the sketch prints the aggregated frame whenever it
changes and accepts commands on the serial console at 115200 baud that fake a
panel byte:

```
a0 1     set panel A byte 0 to 1  - low beams on
a0 0     clear it
b4 4     set panel B byte 4 to 4  - right knob to position 3, wipers low
```

Typing `a0 1` (low beams, IN21) then `a1 1` (high beams, IN22) then `a1 0`
prints the aggregation behaviour the Mastercell spec describes, shifted into
byte 2 because the controls live at IN17 and up:

```
18CF0080  00 00 08 00 00 00 A5 00
18CF0080  00 00 0C 00 00 00 A5 00
18CF0080  00 00 08 00 00 00 A5 00
```

Low beams alone, then both beams, then high beams released while low beams
holds — one bit changing at a time, everything else undisturbed.

Injected state is held alive so the source timeout does not sweep it away
mid-test. As soon as a real frame arrives from either panel, the sketch stops
holding it and normal timeout behaviour resumes.

## Running the tests

The aggregation logic is hardware-free and compiles on a PC:

```bash
./test/run.sh
```

That builds three binaries. The first runs against a synthetic map that reaches
every corner of the frame layout: the bit positions against the spec table, the
two worked examples, aggregation across both panels, the IN38/HSIN01 boundary
inside byte 4, the reserved nibble in byte 5, both match kinds, two-byte
little-endian bitmasks, toggle latching, shared inputs, source timeouts, id
matching that ignores priority and source address, and the PDU1
destination-byte handling described below.

The second runs against the real table in `input_map.h`, so it checks actual
car behaviour by name: each switch driving its own input, switches accumulating
and releasing independently, the wiper and smoke positions, the interior
lighting exclusivity requirement, unused detents driving nothing, a knob caught
between detents asserting neither, switches and knobs coexisting in one frame,
IN01–IN16 staying clear with everything switched on, and one panel timing out
without taking the other down.

The third repeats those production tests with `ROTARY_AS_BITMASK` set to 0, so
the expectations are known to hold whichever way the SW4 encodes a detent.

## How the two panels are told apart

The panels transmit `0x18EF011E` and `0x18EF021E`. That decodes as priority 6,
PF `0xEF`, PS `0x01`/`0x02`, source address `0x1E`.

There is a trap here. `0xEF` is 239, below the 240 threshold that separates
PDU1 from PDU2 format, so these are PDU1 (proprietary A) frames. In PDU1 the
third byte is a **destination address, not part of the PGN**. Both panels are
strictly sending the same PGN, `0xEF00`, addressed to two different nodes.

So the usual advice to "extract the PGN and match on that" is actively wrong
here: it would mask off the `01`/`02` and leave nothing to distinguish the two
panels. The sketch matches on the data page bits plus PF and PS instead, which
ignores priority and source address as required while keeping the destination
byte. `test_aggregator.cpp` has a test for exactly this.

The practical consequence: any tool that reports these frames by PGN will show
both as `0xEF00`, and any device listening for PGN `0xEF00` alone will see the
two panels as one stream.

## Things to confirm before this goes on the car

- **Which byte carries exhaust loud and which carries exhaust quiet.** Bytes 3
  and 4 are assumed, being the two previously called show lights and aux
  switch.
- **The rotary encoding**, bitmask or plain detent number. One `#define`.
- **The input numbers.** IN17–IN27 were allocated as a block and need to line
  up with the inCODE NGX cases.
- **Whether CAN controlled inputs and physical input wires share numbering**,
  which decides how much clearance the rockers need.

Remember that any conditions set on the cases in inCODE NGX still apply. An
input going high here does not activate an output unless those conditions are
also met — the smoke screen is the obvious one, where the middle knob at
position 5 supplies main power and the physical rocker triggers the deploy
loop.
