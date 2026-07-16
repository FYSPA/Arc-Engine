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

class AudioEngine {
  static final _getFlacInfo = _lib.lookupFunction<
      Int32 Function(Pointer<Utf8>, Pointer<FlacInfo>),
      int Function(Pointer<Utf8>, Pointer<FlacInfo>)>('get_flac_info');

  static final _playFlac = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_flac');

  static final _playAudio = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_audio');

  static final _playWav = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('play_wav');

  // Engine controls
  static final _startAudio = _lib.lookupFunction<Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)>('start_audio');

  static final _stopAudio =
      _lib.lookupFunction<Void Function(), void Function()>('stop_audio');

  static final _pauseAudio =
      _lib.lookupFunction<Void Function(), void Function()>('pause_audio');

  static final _resumeAudio =
      _lib.lookupFunction<Void Function(), void Function()>('resume_audio');

  static final _seekAudio = _lib
      .lookupFunction<Int32 Function(Int32), int Function(int)>('seek_audio');

  static final _getPosition =
      _lib.lookupFunction<Int32 Function(), int Function()>('get_position');

  static final _getDuration =
      _lib.lookupFunction<Int32 Function(), int Function()>('get_duration');

  static final _isPlaying =
      _lib.lookupFunction<Int32 Function(), int Function()>('is_playing');

  static Future<Map<String, dynamic>> getFlacInfo(String path) async {
    final pathPtr = path.toNativeUtf8();
    final infoPtr = calloc<FlacInfo>();

    try {
      final result = _getFlacInfo(pathPtr, infoPtr);

      if (result != 0) {
        throw Exception('Failed to read FLAC: error $result');
      }

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
    final pathPtr = path.toNativeUtf8();
    try {
      final result = _playFlac(pathPtr);
      if (result != 0) throw Exception('Playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  static Future<void> playWav(String path) async {
    final pathPtr = path.toNativeUtf8();
    try {
      final result = _playWav(pathPtr);
      if (result != 0) throw Exception('WAV playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  static Future<void> playAudio(String path) async {
    final ext = path.split('.').last.toLowerCase();
    if (ext == 'flac') return playFlac(path);
    if (ext == 'wav') return playWav(path);

    final pathPtr = path.toNativeUtf8();
    try {
      final result = _playAudio(pathPtr);
      if (result != 0) throw Exception('Playback failed: error $result');
    } finally {
      calloc.free(pathPtr);
    }
  }

  // ─── Non-blocking engine controls ────────────────────────────────────────

  static int startAudio(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _startAudio(pathPtr);
    } finally {
      calloc.free(pathPtr);
    }
  }

  static void stop() => _stopAudio();

  static void pause() => _pauseAudio();

  static void resume() => _resumeAudio();

  static int seek(int positionMs) => _seekAudio(positionMs);

  static int getPosition() => _getPosition();

  static int getDuration() => _getDuration();

  static bool get isPlaying => _isPlaying() != 0;
}
