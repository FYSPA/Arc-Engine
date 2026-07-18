import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

final DynamicLibrary _lib = Platform.isAndroid
    ? DynamicLibrary.open('libaudio_engine.so')
    : DynamicLibrary.process();

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

/// Interface for FFI-native operations. Tests can implement this with fake
/// behavior to avoid needing the actual native library.
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
  void mixerSetMasterVolume(double vol);

  int getPcmAvailable();
  int readPcmSamples(Pointer<Float> buffer, int maxFrames);

  void eqSetBand(int index, int type, double freq, double gain, double q);
  void eqSetBandEnabled(int index, int enabled);
  void eqSetBypass(int bypass);
  void eqReset();

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
  void mixerSetMasterVolume(double vol) => _mixerSetMasterVolume(vol);
  late final _mixerSetMasterVolume =
      _lib.lookupFunction<Void Function(Float), void Function(double)>(
          'mixer_set_master_volume');

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
}
