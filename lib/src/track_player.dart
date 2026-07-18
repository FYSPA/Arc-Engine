import 'dart:async';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface;

enum PlaybackState { stopped, playing, paused }

class TrackPlayer {
  final int index;
  final FfiInterface _ffi = FfiInterface.instance;

  Timer? _timer;
  PlaybackState _state = PlaybackState.stopped;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  double _volume = 1.0;
  double _pan = 0.0;

  final StreamController<PlaybackState> _stateCtrl =
      StreamController<PlaybackState>.broadcast();
  final StreamController<Duration> _posCtrl =
      StreamController<Duration>.broadcast();

  Stream<PlaybackState> get onStateChanged => _stateCtrl.stream;
  Stream<Duration> get onPositionChanged => _posCtrl.stream;

  TrackPlayer(this.index);

  PlaybackState get state => _state;
  Duration get position => _position;
  Duration get duration => _duration;
  double get volume => _volume;
  double get pan => _pan;

  set volume(double v) {
    _volume = v.clamp(0.0, 1.0);
    _ffi.trackSetVolume(index, _volume);
  }

  set pan(double p) {
    _pan = p.clamp(-1.0, 1.0);
    _ffi.trackSetPan(index, _pan);
  }

  int play(String path) {
    stop();
    final pathPtr = path.toNativeUtf8();
    try {
      final result = _ffi.trackPlay(index, pathPtr);
      if (result == 0) {
        _state = PlaybackState.playing;
        _stateCtrl.add(_state);
        _startPolling();
      }
      return result;
    } finally {
      calloc.free(pathPtr);
    }
  }

  void stop() {
    _timer?.cancel();
    _timer = null;
    _ffi.trackStop(index);
    _state = PlaybackState.stopped;
    _position = Duration.zero;
    _stateCtrl.add(_state);
    _posCtrl.add(_position);
  }

  void pause() {
    _ffi.trackPause(index);
    _state = PlaybackState.paused;
    _stateCtrl.add(_state);
  }

  void resume() {
    _ffi.trackResume(index);
    _state = PlaybackState.playing;
    _stateCtrl.add(_state);
  }

  void seek(Duration position) {
    _ffi.trackSeek(index, position.inMilliseconds);
    _position = position;
    _posCtrl.add(_position);
  }

  void dispose() {
    stop();
    _stateCtrl.close();
    _posCtrl.close();
  }

  void _startPolling() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      final playing = _ffi.trackIsPlaying(index) != 0;
      final paused = _ffi.trackGetPosition(index) >= 0 &&
          !playing &&
          _ffi.trackGetPosition(index) < _duration.inMilliseconds;

      if (playing) {
        final posMs = _ffi.trackGetPosition(index);
        final durMs = _ffi.trackGetDuration(index);
        final newPos = Duration(milliseconds: posMs);
        final newDur = Duration(milliseconds: durMs);

        if (_state != PlaybackState.playing) {
          _state = PlaybackState.playing;
          _stateCtrl.add(_state);
        }
        if (newPos != _position) {
          _position = newPos;
          _posCtrl.add(_position);
        }
        _duration = newDur;
      } else if (_state == PlaybackState.playing) {
        _timer?.cancel();
        _timer = null;
        _state = PlaybackState.stopped;
        _position = Duration.zero;
        _stateCtrl.add(_state);
        _posCtrl.add(_position);
      }
    });
  }
}
