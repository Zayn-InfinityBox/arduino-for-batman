#pragma once

// Mapping table used only by the host-side tests. It deliberately reaches
// every awkward corner of the frame layout: the first and last bit of a byte,
// the IN38 / HSIN01 boundary inside byte 4, byte 5's reserved nibble, a
// two-byte bitmask field, both match kinds, a toggle row, and two sources
// feeding one input.

static const InputMapEntry INPUT_MAP[] PROGMEM = {
    // src   byte  w  match       value        target     mode
    {SRC_A, 0, 1, MATCH_BITS, ANY_NONZERO, IN(1), MODE_FOLLOW},
    {SRC_A, 1, 1, MATCH_BITS, ANY_NONZERO, IN(2), MODE_FOLLOW},
    {SRC_A, 2, 1, MATCH_BITS, ANY_NONZERO, IN(9), MODE_FOLLOW},
    {SRC_A, 3, 1, MATCH_BITS, ANY_NONZERO, IN(38), MODE_FOLLOW},
    {SRC_A, 4, 1, MATCH_BITS, ANY_NONZERO, HSIN(1), MODE_FOLLOW},
    {SRC_A, 5, 1, MATCH_BITS, ANY_NONZERO, HSIN(2), MODE_FOLLOW},
    {SRC_A, 6, 1, MATCH_BITS, ANY_NONZERO, HSIN(3), MODE_FOLLOW},
    {SRC_A, 7, 1, MATCH_BITS, ANY_NONZERO, HSIN(6), MODE_FOLLOW},

    {SRC_B, 0, 1, MATCH_BITS, ANY_NONZERO, IN(17), MODE_FOLLOW},
    {SRC_B, 1, 1, MATCH_BITS, ANY_NONZERO, IN(32), MODE_FOLLOW},
    {SRC_B, 2, 1, MATCH_BITS, ANY_NONZERO, IN(33), MODE_FOLLOW},

    // Single-byte knob reporting its position as a plain number.
    {SRC_B, 3, 1, MATCH_EQUAL, 1, IN(20), MODE_FOLLOW},
    {SRC_B, 3, 1, MATCH_EQUAL, 2, IN(21), MODE_FOLLOW},
    {SRC_B, 3, 1, MATCH_EQUAL, 3, IN(22), MODE_FOLLOW},

    // Momentary button latched here rather than at the panel.
    {SRC_B, 4, 1, MATCH_BITS, ANY_NONZERO, IN(25), MODE_TOGGLE},

    // Second control wired to an input source A also drives.
    {SRC_B, 5, 1, MATCH_BITS, ANY_NONZERO, IN(2), MODE_FOLLOW},

    // Two-byte bitmask spanning bytes 6-7, little-endian. The 0x0100 position
    // only matches if the high byte is being read, and 0x0001 only if the low
    // one is, so a swapped byte order cannot pass both.
    {SRC_B, 6, 2, MATCH_BITS, 0x0001, IN(26), MODE_FOLLOW},
    {SRC_B, 6, 2, MATCH_BITS, 0x0002, IN(27), MODE_FOLLOW},
    {SRC_B, 6, 2, MATCH_BITS, 0x0100, IN(28), MODE_FOLLOW},
};
