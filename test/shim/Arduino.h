#pragma once

// Minimal stand-in for the Arduino core, just enough to compile the
// hardware-free aggregator logic on a PC.

#include <stdint.h>
#include <string.h>

#define PROGMEM
#define memcpy_P memcpy
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
