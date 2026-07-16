#pragma once

#include <stdint.h>
#include "common.h"

static inline int32_t readInt16LE(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline int32_t readInt32LE(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

extern "C" {
EXPORT int32_t play_wav(const char* path);
}
