// ---------------------------------------------------------------------------
// File: dispatcher.h
// Purpose: FFI-exported C functions for track-based and legacy playback
//          control, EQ configuration, and streaming.
// Importance: The Dart FFI layer binds to these EXPORT symbols.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include "common.h"

void ensureFxChain(float sampleRate, int channels);

extern "C" {
EXPORT int32_t track_play(int32_t index, const char* path);
EXPORT int32_t play_audio(const char* path);
EXPORT int32_t start_audio(const char* path);
EXPORT int32_t start_media_stream(const char* url);
EXPORT void track_set_mute(int32_t index, int32_t mute);
EXPORT void track_set_solo(int32_t index, int32_t solo);
EXPORT void track_set_loop(int32_t index, int32_t loop);
EXPORT void eq_set_band(int32_t index, int32_t type, double freq, double gain, double q);
EXPORT void eq_set_band_enabled(int32_t index, int32_t enabled);
EXPORT void eq_set_bypass(int32_t bypass);
EXPORT void eq_reset();
EXPORT void limiter_set_enabled(int32_t enabled);
EXPORT void limiter_set_threshold(float db);
}
