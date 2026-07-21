// ---------------------------------------------------------------------------
// File: wav_handler.h
// Purpose: WAV RIFF parsing helpers and legacy play_wav export.
// Importance: Enables WAV playback without the multi-track engine.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>
#include "common.h"
#include "engine_state.h"

static inline int32_t readInt16LE(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline int32_t readInt32LE(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

// Read WAV fmt chunk to get sample rate and channels without loading PCM data.
// Returns 0 on success, negative on error.
int32_t getWavFormat(const char *path, int32_t &outSampleRate, int32_t &outChannels);

// Load a WAV file into an existing TrackState (for gapless transitions).
// Returns 0 on success, negative on error.
int32_t loadWavIntoState(TrackState &trk, const char *path);

extern "C" {
EXPORT int32_t play_wav(const char* path);
}
