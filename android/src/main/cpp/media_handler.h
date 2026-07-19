// ---------------------------------------------------------------------------
// File: media_handler.h
// Purpose: Legacy play_media export for compressed audio via AMediaCodec.
// Importance: Enables MP3/AAC/OGG/M4A playback without the multi-track engine.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include "common.h"

extern "C" {
EXPORT int32_t play_media(const char* path);
}
