// ---------------------------------------------------------------------------
// File: flac_handler.h
// Purpose: FLAC decoding callback declarations (metadata, write, error) and
//          public exports (get_flac_info, play_flac).
// Importance: Core FLAC support via libFLAC.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>
#include <aaudio/AAudio.h>
#include <FLAC/stream_decoder.h>
#include "common.h"

struct PlayState {
    FlacInfo info;
    AAudioStream *stream;
    int trackIndex;
};

FLAC__StreamDecoderWriteStatus infoWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*);

void metadataCallback(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata *metadata, void *client_data);

void errorCallback(
    const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus status, void*);

FLAC__StreamDecoderWriteStatus playWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[], void *client_data);

FLAC__StreamDecoderWriteStatus flacEngineWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[], void *client_data);

// Check if FLAC file matches expected sample rate and channels.
// Uses a temporary decoder that is fully cleaned up. Does not modify any state.
bool checkFlacFormatMatch(const char *path, int32_t expectedSampleRate, int32_t expectedChannels);

struct TrackState;
// Pre-decode first MAX_PREDECODE_FRAMES frames of a FLAC file into trk.preBuf.
// Creates a temporary decoder that is fully cleaned up.
void predecodeFlac(TrackState &trk, const char *path);

extern "C" {
EXPORT int32_t get_flac_info(const char* path, FlacInfo* outInfo);
EXPORT int32_t play_flac(const char* path);
}
