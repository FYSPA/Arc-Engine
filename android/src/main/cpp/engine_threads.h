// ---------------------------------------------------------------------------
// File: engine_threads.h
// Purpose: Declarations for the four playback thread functions: WAV, FLAC,
//          local media (AMediaCodec), and streaming media.
// Importance: Each thread type decodes audio and pushes into RingBuffer.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

void wavPlaybackThread(int trackIndex);
void flacPlaybackThread(int trackIndex);
void mediaPlaybackThread(int trackIndex);
void mediaStreamPlaybackThread(int trackIndex);
