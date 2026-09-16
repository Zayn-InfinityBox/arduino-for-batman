#pragma once

#include "input_map.h"

void aggReset();
bool aggHandleFrame(uint32_t canId, const uint8_t *data, uint8_t len, uint32_t nowMs);
void aggInjectByte(uint8_t source, uint8_t byteIndex, uint8_t value, uint32_t nowMs);
void aggTouchSource(uint8_t source, uint32_t nowMs);
uint8_t aggExpireStaleSources(uint32_t nowMs);
void aggBuildPayload(uint8_t out[8]);
uint32_t aggMatchKeyFromId(uint32_t canId);
