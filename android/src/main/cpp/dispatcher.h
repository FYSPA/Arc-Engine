#pragma once

#include "common.h"

extern "C" {
EXPORT int32_t play_audio(const char* path);
EXPORT int32_t start_audio(const char* path);
}
