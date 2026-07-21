// ---------------------------------------------------------------------------
// File: fake_ffi.dart
// Purpose: Mock implementation of FfiInterface for unit tests. Tracks
//          playback state, position, volume, pan in-memory maps.
// Importance: Enables deterministic testing without native library.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

import 'dart:ffi';
import 'package:ffi/ffi.dart';
import 'package:arc_engine/src/ffi_bindings.dart';

class FakeFfi implements FfiInterface {
  final Map<int, bool> _playing = {};
  final Map<int, bool> _paused = {};
  final Map<int, int> _positions = {};
  final Map<int, int> _durations = {};
  final Map<int, double> _volumes = {};
  final Map<int, double> _pans = {};
  final Map<int, bool> _mutes = {};
  final Map<int, bool> _solos = {};
  double masterVol = 1.0;
  int _pcmAvailable = 0;
  List<double> _pcmSamples = [];

  set pcmAvailable(int v) => _pcmAvailable = v;
  set pcmSamples(List<double> v) => _pcmSamples = v;

  Map<int, bool> get playing => _playing;
  Map<int, int> get positions => _positions;
  Map<int, double> get volumes => _volumes;
  Map<int, double> get pans => _pans;
  Map<int, bool> get mutes => _mutes;
  Map<int, bool> get solos => _solos;

  void reset() {
    _playing.clear();
    _paused.clear();
    _positions.clear();
    _durations.clear();
    _volumes.clear();
    _pans.clear();
    _mutes.clear();
    _solos.clear();
    masterVol = 1.0;
    _pcmAvailable = 0;
    _pcmSamples.clear();
  }

  void tick(int index, int ms) {
    final cur = _positions[index] ?? 0;
    _positions[index] = cur + ms;
    final dur = _durations[index] ?? 10000;
    if (cur + ms >= dur) {
      _playing[index] = false;
      _positions[index] = dur;
    }
  }

  @override
  int trackPlay(int index, Pointer<Utf8> path) {
    _playing[index] = true;
    _paused[index] = false;
    _positions[index] = 0;
    _durations[index] = 10000;
    _volumes[index] = 1.0;
    _pans[index] = 0.0;
    return 0;
  }

  @override
  void trackStop(int index) {
    _playing[index] = false;
    _paused[index] = false;
    _positions[index] = 0;
  }

  @override
  void trackPause(int index) {
    _paused[index] = true;
  }

  @override
  void trackResume(int index) {
    _paused[index] = false;
    _playing[index] = true;
  }

  @override
  int trackSeek(int index, int ms) {
    _positions[index] = ms;
    return 0;
  }

  @override
  int trackGetPosition(int index) => _positions[index] ?? 0;

  @override
  int trackGetDuration(int index) => _durations[index] ?? 0;

  @override
  int trackIsPlaying(int index) =>
      (_playing[index] == true && _paused[index] != true) ? 1 : 0;

  @override
  void trackSetVolume(int index, double vol) {
    _volumes[index] = vol;
  }

  @override
  void trackSetPan(int index, double pan) {
    _pans[index] = pan;
  }

  @override
  void mixerSetMasterVolume(double vol) {
    masterVol = vol;
  }

  @override
  void engineSetCrossfadeFrames(int frames) {}

  @override
  int getFlacInfo(Pointer<Utf8> path, Pointer<FlacInfo> info) => -1;

  @override
  int playFlac(Pointer<Utf8> path) => -1;

  @override
  int playAudio(Pointer<Utf8> path) => -1;

  @override
  int playWav(Pointer<Utf8> path) => -1;

  @override
  int startAudio(Pointer<Utf8> path) => -1;

  @override
  int startMediaStream(Pointer<Utf8> url) => -1;

  @override
  void stopAudio() {}

  @override
  void pauseAudio() {}

  @override
  void resumeAudio() {}

  @override
  int seekAudio(int ms) => -1;

  @override
  int getPosition() => trackGetPosition(0);

  @override
  int getDuration() => trackGetDuration(0);

  @override
  int isPlaying() => trackIsPlaying(0);

  @override
  int getPcmAvailable() => _pcmAvailable;

  @override
  int readPcmSamples(Pointer<Float> buffer, int maxFrames) {
    final count =
        _pcmSamples.length > maxFrames * 2 ? maxFrames * 2 : _pcmSamples.length;
    for (int i = 0; i < count; i++) {
      buffer[i] = _pcmSamples[i];
    }
    return count ~/ 2;
  }

  @override
  void eqSetBand(int index, int type, double freq, double gain, double q) {}

  @override
  void eqSetBandEnabled(int index, int enabled) {}

  @override
  void eqSetBypass(int bypass) {}

  @override
  void eqReset() {}

  @override
  void limiterSetEnabled(int enabled) {}

  @override
  void limiterSetThreshold(double db) {}

  @override
  int fxAdd(Pointer<Utf8> name) => 0;

  @override
  int fxRemove(Pointer<Utf8> name) => 0;

  @override
  void fxClear() {}

  @override
  int fxSetEnabled(Pointer<Utf8> name, int enabled) => 0;

  @override
  int trackGetGapLessVersion(int index) => 0;

  @override
  int trackGetGapLessAbort(int index) => 0;

  @override
  void compressorSetThreshold(double db) {}

  @override
  void compressorSetRatio(double r) {}

  @override
  void compressorSetAttack(double ms) {}

  @override
  void compressorSetRelease(double ms) {}

  @override
  void compressorSetKnee(double db) {}

  @override
  void compressorSetMakeup(double db) {}

  // ─── Reverb ──────────────────────────────────────────────────────────
  @override
  void reverbSetMix(double v) {}

  @override
  void reverbSetDecay(double v) {}

  @override
  void reverbSetRoomSize(double v) {}

  @override
  void reverbSetDamping(double v) {}

  @override
  void reverbSetPreDelay(double ms) {}

  @override
  void trackSetMute(int index, int mute) {
    _mutes[index] = mute != 0;
  }

  @override
  void trackSetSolo(int index, int solo) {
    _solos[index] = solo != 0;
  }

  @override
  void trackSetLoop(int index, int loop) {}

  @override
  void trackSetNext(int index, Pointer<Utf8> path) {}

  @override
  void trackClearNext(int index) {}

  @override
  int trackGetPcmAvailable(int index) => _pcmAvailable;

  @override
  int trackReadPcmSamples(int index, Pointer<Float> buffer, int maxFrames) {
    final count =
        _pcmSamples.length > maxFrames * 2 ? maxFrames * 2 : _pcmSamples.length;
    for (int i = 0; i < count; i++) {
      buffer[i] = _pcmSamples[i];
    }
    return count ~/ 2;
  }
}
