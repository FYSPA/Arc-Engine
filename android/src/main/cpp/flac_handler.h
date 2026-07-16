#pragma once

#include <stdint.h>
#include <aaudio/AAudio.h>
#include <FLAC/stream_decoder.h>
#include "common.h"

struct PlayState {
    FlacInfo info;
    AAudioStream *stream;
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

extern "C" {
EXPORT int32_t get_flac_info(const char* path, FlacInfo* outInfo);
EXPORT int32_t play_flac(const char* path);
}
