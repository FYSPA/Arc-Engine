// ---------------------------------------------------------------------------
// File: engine_threads.h
// Purpose: Declarations for the four playback thread functions: WAV, FLAC,
//          local media (AMediaCodec), and streaming media.
// Importance: Each thread type decodes audio and pushes into RingBuffer.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

void wavPlaybackThread(int trackIndex);
void flacPlaybackThread(int trackIndex);
void mediaPlaybackThread(int trackIndex);
void mediaStreamPlaybackThread(int trackIndex);

// Pre-create AAudio stream + DSP/Limiter/FX for given SR/ch.
// Called from track_play() to move stream creation off the decoder thread.
// Returns true if stream already existed with matching SR/ch, or was created successfully.
bool initFirstTrackStream(int32_t sr, int32_t ch);
