// ---------------------------------------------------------------------------
// File: flac_handler.cpp
// Purpose: FLAC decoding callbacks and public exports (get_flac_info,
//          play_flac). Handles both legacy blocking and engine-based
//          playback via flacEngineWriteCallback.
// Importance: Core FLAC decode logic.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "flac_handler.h"
#include "aaudio_utils.h"
#include "engine_state.h"
#include "ring_buffer.h"

#include <cstring>
#include <aaudio/AAudio.h>

// ─── FLAC callbacks ──────────────────────────────────────────────────────────

FLAC__StreamDecoderWriteStatus infoWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*) {
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void metadataCallback(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata *metadata, void *client_data) {

    FlacInfo *info = (FlacInfo*)client_data;
    if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;

    info->totalSamples = metadata->data.stream_info.total_samples;
    info->sampleRate = metadata->data.stream_info.sample_rate;
    info->channels = metadata->data.stream_info.channels;
    info->bitsPerSample = metadata->data.stream_info.bits_per_sample;

    if (info->sampleRate > 0)
        info->durationMs = (int32_t)((info->totalSamples * 1000) / info->sampleRate);
}

void errorCallback(
    const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus status, void*) {
    LOGE("FLAC error: %d", status);
}

FLAC__StreamDecoderWriteStatus playWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[], void *client_data) {

    PlayState *state = (PlayState*)client_data;
    const int32_t frames = frame->header.blocksize;
    const int32_t channels = state->info.channels;
    const float scale = 1.0f / (float)(1LL << (state->info.bitsPerSample - 1));

    float floatBuf[frames * channels];
    for (int32_t i = 0; i < frames; i++)
        for (int32_t ch = 0; ch < channels; ch++)
            floatBuf[i * channels + ch] = buffer[ch][i] * scale;

    return writeFrames(state->stream, floatBuf, frames, channels) == 0
        ? FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE
        : FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
}

FLAC__StreamDecoderWriteStatus flacEngineWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[], void *client_data) {

    PlayState *state = (PlayState*)client_data;
    TrackState &trk = gCtl.tracks[state->trackIndex];

    if (!trk.running) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    if (trk.paused) return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

    const int32_t frames = frame->header.blocksize;
    const int32_t channels = state->info.channels;
    const float scale = 1.0f / (float)(1LL << (state->info.bitsPerSample - 1));

    float floatBuf[frames * channels];
    for (int32_t i = 0; i < frames; i++)
        for (int32_t ch = 0; ch < channels; ch++)
            floatBuf[i * channels + ch] = buffer[ch][i] * scale;

    if (trk.ringBuf) {
        trk.ringBuf->push(floatBuf, frames, channels);
    }
    if (trk.pcmRingBuf) {
        trk.pcmRingBuf->push(floatBuf, frames, channels);
    }

    trk.writtenFrames += frames;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

// ─── get_flac_info ───────────────────────────────────────────────────────────

int32_t get_flac_info(const char* path, FlacInfo* outInfo) {
    memset(outInfo, 0, sizeof(FlacInfo));

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) return -1;

    FLAC__stream_decoder_set_metadata_respond_all(decoder);

    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, infoWriteCallback, metadataCallback, errorCallback, outInfo);

    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        return -2;
    }

    int32_t ok = FLAC__stream_decoder_process_until_end_of_metadata(decoder) ? 0 : -3;
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    return ok;
}

// ─── play_flac ───────────────────────────────────────────────────────────────

int32_t play_flac(const char* path) {
    PlayState state;
    memset(&state, 0, sizeof(state));

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) return -1;

    FLAC__stream_decoder_set_metadata_respond_all(decoder);

    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, playWriteCallback, metadataCallback, errorCallback, &state);

    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        return -2;
    }

    FLAC__stream_decoder_process_until_end_of_metadata(decoder);

    if (state.info.sampleRate == 0 || state.info.channels == 0) {
        FLAC__stream_decoder_delete(decoder);
        return -3;
    }

    state.stream = createAAudioStream(state.info.sampleRate, state.info.channels);
    if (!state.stream) { FLAC__stream_decoder_delete(decoder); return -4; }

    FLAC__stream_decoder_process_until_end_of_stream(decoder);

    closeAAudioStream(state.stream);
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    return 0;
}
