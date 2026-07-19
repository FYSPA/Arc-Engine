// ---------------------------------------------------------------------------
// File: audio_mixer.dart
// Purpose: AudioEngine singleton — the central orchestrator. Holds up to 4
//          TrackPlayer instances, master volume, PCM stream, and backward-
//          compatible static API (startAudio/stop/pause/resume/seek) that
//          delegates to track 0. Also exposes global EQ controls.
// Importance: Main entry point for all consumers. Every app using the engine
//             starts with AudioEngine.instance.
// Missing: - No clipping protection in the mixer (multiple tracks sum can
//            exceed 1.0f and clip at the DAC)
//          - No sample-rate conversion when mixing tracks of different rates
//          - No per-track EQ (only global DSP post-mix)
// Known issues: `startAudio` has an unnecessary `as int` cast that triggers
//               an analyzer warning. Some static methods read from _ffi
//               directly instead of going through TrackPlayer.
// ---------------------------------------------------------------------------

import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface, FlacInfo;
import 'track_player.dart';
import 'pcm_stream.dart';

/// Central orchestrator for the native audio engine.
///
/// Singleton accessed via [AudioEngine.instance].  Manages 4 [TrackPlayer]s,
/// master volume, PCM stream, and global DSP EQ.
///
/// Static methods (`startAudio`, `stop`, `pause`, etc.) delegate to track 0
/// for backward compatibility with the legacy single-track API.
class AudioEngine {
  static final AudioEngine _instance = AudioEngine._();
  static AudioEngine get instance => _instance;

  final FfiInterface _ffi = FfiInterface.instance;
  final List<TrackPlayer> tracks;
  final PcmStream _pcmStream = PcmStream();

  AudioEngine._()
      : tracks = List.unmodifiable(
          List.generate(4, (i) => TrackPlayer(i)),
        );

  /// Master output volume. 0.0 = silent, 1.0 = full. Clamped to 0.0–1.0.
  double get masterVolume => _masterVol;
  double _masterVol = 1.0;
  set masterVolume(double v) {
    _masterVol = v.clamp(0.0, 1.0);
    _ffi.mixerSetMasterVolume(_masterVol);
  }

  Stream<List<double>> startPcmStream({
    Duration interval = const Duration(milliseconds: 50),
  }) =>
      _pcmStream.start(interval: interval);

  void stopPcmStream() => _pcmStream.stop();

  // ─── Backward compat (static, delegates to track 0) ──────────────────

  static TrackPlayer get _t0 => instance.tracks[0];

  static int startAudio(String path) => _t0.play(path);

  static int streamUrl(String url) {
    final ffi = FfiInterface.instance;
    final pathPtr = url.toNativeUtf8();
    try {
      return ffi.startMediaStream(pathPtr);
    } finally {
      calloc.free(pathPtr);
    }
  }

  static void stop() => _t0.stop();
  static void pause() => _t0.pause();
  static void resume() => _t0.resume();
  static int seek(int positionMs) {
    _t0.seek(Duration(milliseconds: positionMs));
    return 0;
  }

  static int getPosition() => _t0.position.inMilliseconds;
  static int getDuration() => _t0.duration.inMilliseconds;
  static bool get isPlaying => _t0.state == PlaybackState.playing;

  static int getPcmAvailable() => FfiInterface.instance.getPcmAvailable();

  static int readPcmSamples(Pointer<Float> buffer, int maxFrames) =>
      FfiInterface.instance.readPcmSamples(buffer, maxFrames);

  // ─── FLAC info ──────────────────────────────────────────────────────
  static Future<Map<String, dynamic>> getFlacInfo(String path) async {
    final ffi = FfiInterface.instance;
    final pathPtr = path.toNativeUtf8();
    final infoPtr = calloc<FlacInfo>();
    try {
      final result = ffi.getFlacInfo(pathPtr, infoPtr);
      if (result != 0) throw Exception('Failed to read FLAC: error $result');
      final info = infoPtr.ref;
      return {
        'sampleRate': info.sampleRate,
        'channels': info.channels,
        'bitsPerSample': info.bitsPerSample,
        'totalSamples': info.totalSamples,
        'durationMs': info.durationMs,
      };
    } finally {
      calloc.free(pathPtr);
      calloc.free(infoPtr);
    }
  }

  static Future<void> playFlac(String path) async {
    final ffi = FfiInterface.instance;
    final pathPtr = path.toNativeUtf8();
    try {
      final result = ffi.playFlac(pathPtr);
      if (result != 0) throw Exception('Playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  static Future<void> playWav(String path) async {
    final ffi = FfiInterface.instance;
    final pathPtr = path.toNativeUtf8();
    try {
      final result = ffi.playWav(pathPtr);
      if (result != 0) throw Exception('WAV playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  static Future<void> playAudio(String path) async {
    final ext = path.split('.').last.toLowerCase();
    if (ext == 'flac') return playFlac(path);
    if (ext == 'wav') return playWav(path);
    final ffi = FfiInterface.instance;
    final pathPtr = path.toNativeUtf8();
    try {
      final result = ffi.playAudio(pathPtr);
      if (result != 0) throw Exception('Playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  // ─── EQ (global) ────────────────────────────────────────────────────
  static const int eqPeaking = 0;
  static const int eqLowShelf = 1;
  static const int eqHighShelf = 2;
  static const int eqLowPass = 3;
  static const int eqHighPass = 4;

  static void setEqBand(
          int index, int type, double freq, double gain, double q) =>
      FfiInterface.instance.eqSetBand(index, type, freq, gain, q);

  static void setEqBandEnabled(int index, bool enabled) =>
      FfiInterface.instance.eqSetBandEnabled(index, enabled ? 1 : 0);

  static void setEqBypass(bool bypass) =>
      FfiInterface.instance.eqSetBypass(bypass ? 1 : 0);

  static void resetEq() => FfiInterface.instance.eqReset();
}
