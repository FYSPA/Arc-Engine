// ---------------------------------------------------------------------------
// File: ffi_bindings.dart
// Purpose: Defines all FFI lookupFunction bindings to the native
//          libaudio_engine.so library. Also declares FfiInterface (abstract
//          class for testability) and FlacInfo (native struct).
// Importance: Single source of truth for Dart ↔ C++ FFI bridge. Every native
//             call in the engine goes through this file. The FfiInterface
//             abstraction enables full unit-testing without a real device.
// Missing: - No iOS/macOS/Windows/Linux DynamicLibrary loading paths
//          - lookupFunction errors could provide better diagnostics when the
//            native .so fails to load
// Known issues: DynamicLibrary.open('libaudio_engine.so') will throw on
//               non-Android platforms; the Platform.isAndroid guard prevents
//               this but non-Android fallback (DynamicLibrary.process()) is
//               only suitable for testing on desktop
// ---------------------------------------------------------------------------

import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

final DynamicLibrary _lib = Platform.isAndroid
    ? DynamicLibrary.open('libaudio_engine.so')
    : DynamicLibrary.process();

/// Native FLAC file information struct.
///
/// Returned by [FfiInterface.getFlacInfo], mirrors the C `FlacInfo` struct
/// in `common.h`. Contains metadata from a FLAC file's STREAMINFO block.
final class FlacInfo extends Struct {
  @Int64()
  external int totalSamples;

  @Int32()
  external int sampleRate;

  @Int32()
  external int channels;

  @Int32()
  external int bitsPerSample;

  @Int32()
  external int durationMs;
}

/// Interface for FFI-native operations.
///
/// Tests can implement this with fake behavior to avoid needing the actual
/// native library. Set via [FfiInterface.instance] setter.
///
/// All methods map directly to C EXPORT symbols in libaudio_engine.so.
abstract class FfiInterface {
  int getFlacInfo(Pointer<Utf8> path, Pointer<FlacInfo> info);
  int playFlac(Pointer<Utf8> path);
  int playAudio(Pointer<Utf8> path);
  int playWav(Pointer<Utf8> path);
  int startAudio(Pointer<Utf8> path);
  int startMediaStream(Pointer<Utf8> url);
  void stopAudio();
  void pauseAudio();
  void resumeAudio();
  int seekAudio(int ms);
  int getPosition();
  int getDuration();
  int isPlaying();

  int trackPlay(int index, Pointer<Utf8> path);
  void trackStop(int index);
  void trackPause(int index);
  void trackResume(int index);
  int trackSeek(int index, int ms);
  int trackGetPosition(int index);
  int trackGetDuration(int index);
  int trackIsPlaying(int index);
  void trackSetVolume(int index, double vol);
  void trackSetPan(int index, double pan);
  void trackSetMute(int index, int mute);
  void trackSetSolo(int index, int solo);
  void trackSetLoop(int index, int loop);
  void trackSetNext(int index, Pointer<Utf8> path);
  void trackClearNext(int index);
  void mixerSetMasterVolume(double vol);
  void engineSetCrossfadeFrames(int frames);

  int getPcmAvailable();
  int readPcmSamples(Pointer<Float> buffer, int maxFrames);
  int trackGetPcmAvailable(int index);
  int trackReadPcmSamples(int index, Pointer<Float> buffer, int maxFrames);

  // ─── FX Chain ────────────────────────────────────────────────────────
  int fxAdd(Pointer<Utf8> name);
  int fxRemove(Pointer<Utf8> name);
  void fxClear();
  int fxSetEnabled(Pointer<Utf8> name, int enabled);

  int trackGetGapLessVersion(int index);
  int trackGetGapLessAbort(int index);

  // ─── Compressor ──────────────────────────────────────────────────────
  void compressorSetThreshold(double db);
  void compressorSetRatio(double r);
  void compressorSetAttack(double ms);
  void compressorSetRelease(double ms);
  void compressorSetKnee(double db);
  void compressorSetMakeup(double db);

  // ─── Reverb ──────────────────────────────────────────────────────────
  void reverbSetMix(double v);
  void reverbSetDecay(double v);
  void reverbSetRoomSize(double v);
  void reverbSetDamping(double v);
  void reverbSetPreDelay(double ms);

  void eqSetBand(int index, int type, double freq, double gain, double q);
  void eqSetBandEnabled(int index, int enabled);
  void eqSetBypass(int bypass);
  void eqReset();

  void limiterSetEnabled(int enabled);
  void limiterSetThreshold(double db);

  static FfiInterface get instance => FfiBindings.instance;
  static set instance(FfiInterface v) => FfiBindings._instanceForTest = v;
}

final class FfiBindings implements FfiInterface {
  static final FfiBindings _instance = FfiBindings._();
  static FfiInterface get instance {
    if (_instanceForTest case final v?) return v;
    return _instance;
  }

  static FfiInterface? _instanceForTest;

  FfiBindings._();

  // ─── FLAC ───────────────────────────────────────────────────────────
  @override
  int getFlacInfo(Pointer<Utf8> path, Pointer<FlacInfo> info) =>
      _getFlacInfo(path, info);
  late final _getFlacInfo = _lib.lookupFunction<
      Int32 Function(Pointer<Utf8>, Pointer<FlacInfo>),
      int Function(Pointer<Utf8>, Pointer<FlacInfo>)>('get_flac_info');

  @override
  int playFlac(Pointer<Utf8> path) => _playFlac(path);
  late final _playFlac = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_flac');

  // ─── Legacy play ────────────────────────────────────────────────────
  @override
  int playAudio(Pointer<Utf8> path) => _playAudio(path);
  late final _playAudio = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_audio');

  @override
  int playWav(Pointer<Utf8> path) => _playWav(path);
  late final _playWav = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_wav');

  @override
  int startAudio(Pointer<Utf8> path) => _startAudio(path);
  late final _startAudio = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('start_audio');

  @override
  int startMediaStream(Pointer<Utf8> url) => _startMediaStream(url);
  late final _startMediaStream = _lib.lookupFunction<
      Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('start_media_stream');

  @override
  void stopAudio() => _stopAudio();
  late final _stopAudio =
      _lib.lookupFunction<Void Function(), void Function()>('stop_audio');

  @override
  void pauseAudio() => _pauseAudio();
  late final _pauseAudio =
      _lib.lookupFunction<Void Function(), void Function()>('pause_audio');

  @override
  void resumeAudio() => _resumeAudio();
  late final _resumeAudio =
      _lib.lookupFunction<Void Function(), void Function()>('resume_audio');

  @override
  int seekAudio(int ms) => _seekAudio(ms);
  late final _seekAudio = _lib
      .lookupFunction<Int32 Function(Int32), int Function(int)>('seek_audio');

  @override
  int getPosition() => _getPosition();
  late final _getPosition =
      _lib.lookupFunction<Int32 Function(), int Function()>('get_position');

  @override
  int getDuration() => _getDuration();
  late final _getDuration =
      _lib.lookupFunction<Int32 Function(), int Function()>('get_duration');

  @override
  int isPlaying() => _isPlaying();
  late final _isPlaying =
      _lib.lookupFunction<Int32 Function(), int Function()>('is_playing');

  // ─── Multi-track ────────────────────────────────────────────────────
  @override
  int trackPlay(int index, Pointer<Utf8> path) => _trackPlay(index, path);
  late final _trackPlay = _lib.lookupFunction<
      Int32 Function(Int32, Pointer<Utf8>),
      int Function(int, Pointer<Utf8>)>('track_play');

  @override
  void trackStop(int index) => _trackStop(index);
  late final _trackStop = _lib
      .lookupFunction<Void Function(Int32), void Function(int)>('track_stop');

  @override
  void trackPause(int index) => _trackPause(index);
  late final _trackPause = _lib
      .lookupFunction<Void Function(Int32), void Function(int)>('track_pause');

  @override
  void trackResume(int index) => _trackResume(index);
  late final _trackResume = _lib
      .lookupFunction<Void Function(Int32), void Function(int)>('track_resume');

  @override
  int trackSeek(int index, int ms) => _trackSeek(index, ms);
  late final _trackSeek =
      _lib.lookupFunction<Int32 Function(Int32, Int32), int Function(int, int)>(
          'track_seek');

  @override
  int trackGetPosition(int index) => _trackGetPosition(index);
  late final _trackGetPosition =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_get_position');

  @override
  int trackGetDuration(int index) => _trackGetDuration(index);
  late final _trackGetDuration =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_get_duration');

  @override
  int trackIsPlaying(int index) => _trackIsPlaying(index);
  late final _trackIsPlaying =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_is_playing');

  @override
  void trackSetVolume(int index, double vol) => _trackSetVolume(index, vol);
  late final _trackSetVolume = _lib.lookupFunction<Void Function(Int32, Float),
      void Function(int, double)>('track_set_volume');

  @override
  void trackSetPan(int index, double pan) => _trackSetPan(index, pan);
  late final _trackSetPan = _lib.lookupFunction<Void Function(Int32, Float),
      void Function(int, double)>('track_set_pan');

  @override
  void trackSetMute(int index, int mute) => _trackSetMute(index, mute);
  late final _trackSetMute =
      _lib.lookupFunction<Void Function(Int32, Int32), void Function(int, int)>(
          'track_set_mute');

  @override
  void trackSetSolo(int index, int solo) => _trackSetSolo(index, solo);
  late final _trackSetSolo =
      _lib.lookupFunction<Void Function(Int32, Int32), void Function(int, int)>(
          'track_set_solo');

  @override
  void trackSetLoop(int index, int loop) => _trackSetLoop(index, loop);
  late final _trackSetLoop =
      _lib.lookupFunction<Void Function(Int32, Int32), void Function(int, int)>(
          'track_set_loop');

  @override
  void mixerSetMasterVolume(double vol) => _mixerSetMasterVolume(vol);
  late final _mixerSetMasterVolume =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'mixer_set_master_volume');

  @override
  void engineSetCrossfadeFrames(int frames) =>
      _engineSetCrossfadeFrames(frames);
  late final _engineSetCrossfadeFrames =
      _lib.lookupFunction<Void Function(Int32), void Function(int)>(
          'engine_set_crossfade_frames');

  // ─── PCM Stream ─────────────────────────────────────────────────────
  @override
  int getPcmAvailable() => _getPcmAvailable();
  late final _getPcmAvailable = _lib
      .lookupFunction<Int32 Function(), int Function()>('get_pcm_available');

  @override
  int readPcmSamples(Pointer<Float> buffer, int maxFrames) =>
      _readPcmSamples(buffer, maxFrames);
  late final _readPcmSamples = _lib.lookupFunction<
      Int32 Function(Pointer<Float>, Int32),
      int Function(Pointer<Float>, int)>('read_pcm_samples');

  // ─── Gapless next-track ──────────────────────────────────────────
  @override
  void trackSetNext(int index, Pointer<Utf8> path) =>
      _trackSetNext(index, path);
  late final _trackSetNext = _lib.lookupFunction<
      Void Function(Int32, Pointer<Utf8>),
      void Function(int, Pointer<Utf8>)>('track_set_next');

  @override
  void trackClearNext(int index) => _trackClearNext(index);
  late final _trackClearNext =
      _lib.lookupFunction<Void Function(Int32), void Function(int)>(
          'track_clear_next');

  // ─── Per-track PCM Stream ──────────────────────────────────────────
  @override
  int trackGetPcmAvailable(int index) => _trackGetPcmAvailable(index);
  late final _trackGetPcmAvailable =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_get_pcm_available');

  @override
  int trackReadPcmSamples(int index, Pointer<Float> buffer, int maxFrames) =>
      _trackReadPcmSamples(index, buffer, maxFrames);
  late final _trackReadPcmSamples = _lib.lookupFunction<
      Int32 Function(Int32, Pointer<Float>, Int32),
      int Function(int, Pointer<Float>, int)>('track_read_pcm_samples');

  // ─── FX Chain ──────────────────────────────────────────────────────
  @override
  int fxAdd(Pointer<Utf8> name) => _fxAdd(name);
  late final _fxAdd = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('fx_add');

  @override
  int fxRemove(Pointer<Utf8> name) => _fxRemove(name);
  late final _fxRemove = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('fx_remove');

  @override
  void fxClear() => _fxClear();
  late final _fxClear =
      _lib.lookupFunction<Void Function(), void Function()>('fx_clear');

  @override
  int fxSetEnabled(Pointer<Utf8> name, int enabled) =>
      _fxSetEnabled(name, enabled);
  late final _fxSetEnabled = _lib.lookupFunction<
      Int32 Function(Pointer<Utf8>, Int32),
      int Function(Pointer<Utf8>, int)>('fx_set_enabled');

  // ─── Gap-less version ───────────────────────────────────────────────
  @override
  int trackGetGapLessVersion(int index) => _trackGetGapLessVersion(index);
  late final _trackGetGapLessVersion =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_get_gap_less_version');

  @override
  int trackGetGapLessAbort(int index) => _trackGetGapLessAbort(index);
  late final _trackGetGapLessAbort =
      _lib.lookupFunction<Int32 Function(Int32), int Function(int)>(
          'track_get_gap_less_abort');

  // ─── Compressor ──────────────────────────────────────────────────────
  @override
  void compressorSetThreshold(double db) => _compressorSetThreshold(db);
  late final _compressorSetThreshold =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_threshold');

  @override
  void compressorSetRatio(double r) => _compressorSetRatio(r);
  late final _compressorSetRatio =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_ratio');

  @override
  void compressorSetAttack(double ms) => _compressorSetAttack(ms);
  late final _compressorSetAttack =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_attack');

  @override
  void compressorSetRelease(double ms) => _compressorSetRelease(ms);
  late final _compressorSetRelease =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_release');

  @override
  void compressorSetKnee(double db) => _compressorSetKnee(db);
  late final _compressorSetKnee =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_knee');

  @override
  void compressorSetMakeup(double db) => _compressorSetMakeup(db);
  late final _compressorSetMakeup =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'compressor_set_makeup');

  // ─── Reverb ──────────────────────────────────────────────────────────
  @override
  void reverbSetMix(double v) => _reverbSetMix(v);
  late final _reverbSetMix =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'reverb_set_mix');

  @override
  void reverbSetDecay(double v) => _reverbSetDecay(v);
  late final _reverbSetDecay =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'reverb_set_decay');

  @override
  void reverbSetRoomSize(double v) => _reverbSetRoomSize(v);
  late final _reverbSetRoomSize =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'reverb_set_room_size');

  @override
  void reverbSetDamping(double v) => _reverbSetDamping(v);
  late final _reverbSetDamping =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'reverb_set_damping');

  @override
  void reverbSetPreDelay(double ms) => _reverbSetPreDelay(ms);
  late final _reverbSetPreDelay =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'reverb_set_pre_delay');

  // ─── EQ ─────────────────────────────────────────────────────────────
  @override
  void eqSetBand(int index, int type, double freq, double gain, double q) =>
      _eqSetBand(index, type, freq, gain, q);
  late final _eqSetBand = _lib.lookupFunction<
      Void Function(Int32, Int32, Double, Double, Double),
      void Function(int, int, double, double, double)>('eq_set_band');

  @override
  void eqSetBandEnabled(int index, int enabled) =>
      _eqSetBandEnabled(index, enabled);
  late final _eqSetBandEnabled =
      _lib.lookupFunction<Void Function(Int32, Int32), void Function(int, int)>(
          'eq_set_band_enabled');

  @override
  void eqSetBypass(int bypass) => _eqSetBypass(bypass);
  late final _eqSetBypass =
      _lib.lookupFunction<Void Function(Int32), void Function(int)>(
          'eq_set_bypass');

  @override
  void eqReset() => _eqReset();
  late final _eqReset =
      _lib.lookupFunction<Void Function(), void Function()>('eq_reset');

  // ─── Limiter ─────────────────────────────────────────────────────────
  @override
  void limiterSetEnabled(int enabled) => _limiterSetEnabled(enabled);
  late final _limiterSetEnabled =
      _lib.lookupFunction<Void Function(Int32), void Function(int)>(
          'limiter_set_enabled');

  @override
  void limiterSetThreshold(double db) => _limiterSetThreshold(db);
  late final _limiterSetThreshold =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'limiter_set_threshold');
}
