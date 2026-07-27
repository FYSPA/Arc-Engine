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
#include "common.h"

#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <aaudio/AAudio.h>
#include <FLAC/metadata.h>

// ─── FLAC callbacks ──────────────────────────────────────────────────────────

FLAC__StreamDecoderWriteStatus infoWriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*) {
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void noOpMetadataCallback(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*) {
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

    std::vector<float> floatBuf(frames * channels);
    for (int32_t i = 0; i < frames; i++)
        for (int32_t ch = 0; ch < channels; ch++)
            floatBuf[i * channels + ch] = buffer[ch][i] * scale;

    return writeFrames(state->stream, floatBuf.data(), frames, channels) == 0
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

    std::vector<float> floatBuf(frames * channels);
    for (int32_t i = 0; i < frames; i++)
        for (int32_t ch = 0; ch < channels; ch++) {
            float s = buffer[ch][i] * scale;
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            floatBuf[i * channels + ch] = s;
        }

    if (trk.ringBuf) {
        // ─── CROSSFADE: mix old decoder frames with preBuf in real-time ───
        if (trk.crossfading.load() && trk.preBuf && trk.crossfadeRemaining.load() > 0
            && trk.crossfadePreBufPos < trk.preBufFrames) {
            // Prepare old frames (resample if SR mismatch)
            float *oldFrames = floatBuf.data();
            int32_t oldCount = frames;
            std::vector<float> resampledOld;
            if (trk.resampleToStream && trk.streamSampleRate > 0 && trk.sampleRate > 0
                && trk.streamSampleRate != trk.sampleRate) {
                double ratio = (double)trk.streamSampleRate / trk.sampleRate;
                int32_t extLen = trk.resampleOverlapCount + frames;
                int32_t skipOutput = (trk.resampleOverlapCount > 0)
                    ? (int32_t)(trk.resampleOverlapCount * ratio + 0.5) : 0;
                int32_t totalOut = (int32_t)(extLen * ratio + 0.5);
                int32_t outFrames = totalOut - skipOutput;
                if (outFrames > 0 && outFrames <= frames * 2) {
                    resampledOld.resize(outFrames * channels);
                    resampleSincStream(resampledOld.data(), outFrames, floatBuf.data(), frames, channels, ratio,
                                       trk.resampleOverlap, trk.resampleOverlapCount);
                    for (int32_t i = 0; i < outFrames * channels; i++) {
                        if (resampledOld[i] > 1.0f) resampledOld[i] = 1.0f;
                        else if (resampledOld[i] < -1.0f) resampledOld[i] = -1.0f;
                    }
                    oldFrames = resampledOld.data();
                    oldCount = outFrames;
                }
            }
            // Mix old + new with crossfade curves
            int32_t fadeLen = trk.fadeLen.load();
            int32_t remaining = trk.crossfadeRemaining.load();
            int32_t mixCount = std::min(oldCount, remaining);
            mixCount = std::min(mixCount, trk.preBufFrames - trk.crossfadePreBufPos);
            int32_t startPos = fadeLen - remaining;
            std::vector<float> mixed(mixCount * channels);
            for (int32_t i = 0; i < mixCount; i++) {
                float t = (float)(startPos + i) / fadeLen;
                float fadeOut = cosf(t * 1.57079632679f);
                float fadeIn = sinf(t * 1.57079632679f);
                for (int32_t c = 0; c < channels; c++) {
                    float o = oldFrames[i * channels + c] * fadeOut;
                    float n = trk.preBuf[trk.crossfadePreBufPos * channels + c] * fadeIn;
                    float m = o + n;
                    if (m > 1.0f) m = 1.0f; else if (m < -1.0f) m = -1.0f;
                    mixed[i * channels + c] = m;
                }
                trk.crossfadePreBufPos++;
            }
            trk.ringBuf->push(mixed.data(), mixCount, channels);
            trk.crossfadeRemaining -= mixCount;
            trk.lastFrame[0] = mixed[(mixCount - 1) * channels];
            if (channels > 1) trk.lastFrame[1] = mixed[(mixCount - 1) * channels + 1];
            // Push any remaining old frames (after crossfade ends) at full volume
            if (oldCount > mixCount) {
                trk.ringBuf->push(oldFrames + mixCount * channels, oldCount - mixCount, channels);
                trk.lastFrame[0] = oldFrames[(oldCount - 1) * channels];
                if (channels > 1) trk.lastFrame[1] = oldFrames[(oldCount - 1) * channels + 1];
            }
        } else if (trk.resampleToStream && trk.streamSampleRate > 0 && trk.sampleRate > 0
            && trk.streamSampleRate != trk.sampleRate) {
            double ratio = (double)trk.streamSampleRate / trk.sampleRate;
            int32_t extLen = trk.resampleOverlapCount + frames;
            int32_t skipOutput = (trk.resampleOverlapCount > 0)
                ? (int32_t)(trk.resampleOverlapCount * ratio + 0.5) : 0;
            int32_t totalOut = (int32_t)(extLen * ratio + 0.5);
            int32_t outFrames = totalOut - skipOutput;
            if (outFrames > 0 && outFrames <= frames * 2) {
                std::vector<float> resampled(outFrames * channels);
                resampleSincStream(resampled.data(), outFrames, floatBuf.data(), frames, channels, ratio,
                                   trk.resampleOverlap, trk.resampleOverlapCount);
                for (int32_t i = 0; i < outFrames * channels; i++) {
                    if (resampled[i] > 1.0f) resampled[i] = 1.0f;
                    else if (resampled[i] < -1.0f) resampled[i] = -1.0f;
                }
                updateFadeHistory(trk, resampled.data(), outFrames, channels);
                applyFadeIn(trk, resampled.data(), outFrames, channels);
                trk.ringBuf->push(resampled.data(), outFrames, channels);
                trk.lastFrame[0] = resampled[(outFrames - 1) * channels];
                if (channels > 1) trk.lastFrame[1] = resampled[(outFrames - 1) * channels + 1];
            }
        } else {
            updateFadeHistory(trk, floatBuf.data(), frames, channels);
            applyFadeIn(trk, floatBuf.data(), frames, channels);
            trk.ringBuf->push(floatBuf.data(), frames, channels);
            trk.lastFrame[0] = floatBuf[(frames - 1) * channels];
            if (channels > 1) trk.lastFrame[1] = floatBuf[(frames - 1) * channels + 1];
        }
    }
    if (trk.pcmRingBuf) {
        trk.pcmRingBuf->push(floatBuf.data(), frames, channels);
    }

    trk.writtenFrames += frames;

    // Push position to Dart
    if (gCtl.sampleRate > 0) {
        int64_t posMs = trk.writtenFrames.load() * 1000 / gCtl.sampleRate;
        pushPositionToDart(state->trackIndex, posMs, true, trk.lastCallbackMs, gCtl.dartPort);
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

// ─── checkFlacFormatMatch ─────────────────────────────────────────────────────

bool checkFlacFormatMatch(const char *path, int32_t expectedSampleRate, int32_t expectedChannels) {
    FlacInfo info;
    memset(&info, 0, sizeof(info));

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) return false;

    FLAC__stream_decoder_set_metadata_respond_all(decoder);

    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, infoWriteCallback, metadataCallback, errorCallback, &info);

    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        return false;
    }

    if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
        LOGE("  format check: metadata failed — file may be corrupted: %s", path);
        FLAC__stream_decoder_finish(decoder);
        FLAC__stream_decoder_delete(decoder);
        return false;
    }
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);

    if (info.sampleRate != expectedSampleRate || info.channels != expectedChannels) {
        LOGI("  format check: expected %dHz/%dch, got %dHz/%dch",
             expectedSampleRate, expectedChannels, info.sampleRate, info.channels);
        return false;
    }
    return true;
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

// ─── analyzeFlacWaveform ──────────────────────────────────────────────────

struct WaveformAnalysisCtx {
    int32_t numBars;
    int32_t channels;
    int32_t bitsPerSample;
    int64_t totalSamples;
    int64_t samplesPerBar;
    float *peaks;
};

static FLAC__StreamDecoderWriteStatus analysisWriteCallback(
    const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
    const FLAC__int32 *const buffer[], void *client_data) {

    (void)decoder;
    WaveformAnalysisCtx *ctx = (WaveformAnalysisCtx *)client_data;
    int32_t frames = (int32_t)frame->header.blocksize;
    float scale = 1.0f / (float)(1LL << (ctx->bitsPerSample - 1));

    // Calculate global sample offset from frame header
    int64_t frameStart;
    if (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER)
        frameStart = (int64_t)frame->header.number.sample_number;
    else
        frameStart = (int64_t)frame->header.number.frame_number * frame->header.blocksize;

    for (int32_t f = 0; f < frames; f++) {
        int64_t globalSample = frameStart + f;
        int32_t bar = (int32_t)(globalSample / ctx->samplesPerBar);
        if (bar >= ctx->numBars) break;

        for (int c = 0; c < ctx->channels; c++) {
            float val = fabsf((float)buffer[c][f] * scale);
            if (val > ctx->peaks[bar])
                ctx->peaks[bar] = val;
        }
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

int32_t analyzeFlacWaveform(const char *path, int32_t numBars, float *outPeaks) {
    if (numBars <= 0 || numBars > 512 || !outPeaks) return -1;

    // Initialize peaks to zero
    memset(outPeaks, 0, numBars * sizeof(float));

    // Get metadata first
    FlacInfo info;
    memset(&info, 0, sizeof(info));
    if (get_flac_info(path, &info) != 0) return -2;
    if (info.totalSamples == 0 || info.sampleRate == 0) return -3;

    // Create temporary decoder
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) return -4;

    FLAC__stream_decoder_set_metadata_respond_all(decoder);

    WaveformAnalysisCtx ctx;
    ctx.numBars = numBars;
    ctx.channels = info.channels;
    ctx.bitsPerSample = info.bitsPerSample;
    ctx.totalSamples = info.totalSamples;
    ctx.samplesPerBar = info.totalSamples / numBars;
    ctx.peaks = outPeaks;

    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, analysisWriteCallback, noOpMetadataCallback, errorCallback, &ctx);

    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        return -5;
    }

    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    FLAC__stream_decoder_process_until_end_of_stream(decoder);
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);

    // Normalize: find max peak and scale to 1.0
    float maxPeak = 0.0f;
    for (int32_t i = 0; i < numBars; i++) {
        if (outPeaks[i] > maxPeak) maxPeak = outPeaks[i];
    }
    if (maxPeak > 0.0f && maxPeak < 1.0f) {
        float invMax = 1.0f / maxPeak;
        for (int32_t i = 0; i < numBars; i++) {
            outPeaks[i] *= invMax;
        }
    }

    // Ensure minimum visibility (0.02 floor so empty bars aren't invisible)
    for (int32_t i = 0; i < numBars; i++) {
        if (outPeaks[i] < 0.02f) outPeaks[i] = 0.02f;
    }

    return 0;
}// ─── get_flac_metadata ──────────────────────────────────────────────────────

static void copyVorbisComment(const FLAC__StreamMetadata *tags,
                              const char *key, char *out, int32_t outSize) {
    out[0] = '\0';
    int idx = FLAC__metadata_object_vorbiscomment_find_entry_from(tags, 0, key);
    if (idx < 0) return;
    char *name = nullptr;
    char *value = nullptr;
    if (FLAC__metadata_object_vorbiscomment_entry_to_name_value_pair(
            tags->data.vorbis_comment.comments[idx], &name, &value)) {
        if (value) {
            strncpy(out, value, outSize - 1);
            out[outSize - 1] = '\0';
        }
    }
    if (name) free(name);
    if (value) free(value);
}

int32_t get_flac_metadata(const char* path, FlacMetadata* out) {
    memset(out, 0, sizeof(FlacMetadata));

    // ── Technical properties via streaminfo ────────────────────────────────
    FLAC__StreamMetadata si;
    memset(&si, 0, sizeof(si));
    if (!FLAC__metadata_get_streaminfo(path, &si)) return -2;

    out->sampleRate = (int32_t)si.data.stream_info.sample_rate;
    out->channels = (int32_t)si.data.stream_info.channels;
    out->bitDepth = (int32_t)si.data.stream_info.bits_per_sample;
    out->totalSamples = (int64_t)si.data.stream_info.total_samples;
    if (out->sampleRate > 0)
        out->durationMs = (int32_t)((out->totalSamples * 1000) / out->sampleRate);

    // ── Bitrate from file size / duration ──────────────────────────────────
    struct stat st_file;
    if (stat(path, &st_file) == 0 && out->durationMs > 0) {
        double durSec = (double)out->durationMs / 1000.0;
        out->bitrate = (int32_t)((st_file.st_size * 8.0) / durSec / 1000.0);
    }

    // ── Vorbis Comments (title, artist, album, trackNumber, year) ─────────
    FLAC__StreamMetadata *tags = nullptr;
    if (FLAC__metadata_get_tags(path, &tags) && tags) {
        copyVorbisComment(tags, "TITLE", out->title, sizeof(out->title));
        copyVorbisComment(tags, "ARTIST", out->artist, sizeof(out->artist));
        copyVorbisComment(tags, "ALBUM", out->album, sizeof(out->album));

        char buf[32];
        copyVorbisComment(tags, "TRACKNUMBER", buf, sizeof(buf));
        if (buf[0]) out->trackNumber = atoi(buf);

        copyVorbisComment(tags, "DATE", buf, sizeof(buf));
        if (buf[0]) out->year = atoi(buf);

        FLAC__metadata_object_delete(tags);
    }

    // ── CUESHEET (ISRC) ───────────────────────────────────────────────────
    FLAC__StreamMetadata *cue = nullptr;
    if (FLAC__metadata_get_cuesheet(path, &cue) && cue) {
        if (cue->data.cue_sheet.num_tracks > 0) {
            strncpy(out->isrc, cue->data.cue_sheet.tracks[0].isrc, 12);
            out->isrc[12] = '\0';
        }
        FLAC__metadata_object_delete(cue);
    }

    return 0;
}
