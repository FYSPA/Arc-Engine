// ---------------------------------------------------------------------------
// File: track_player.dart
// Purpose: High-level Dart API for individual audio track playback. Wraps
//          native track_play/stop/pause/resume/seek calls into an observable
//          object with Stream-based state and position notifications.
// Importance: Core user-facing API — every audio track in the engine is
//             controlled through this class. Used by AudioEngine.tracks[i].
// Missing: - No automatic reconnection if native library unloads
//          - Polling Timer (250ms) should be replaced by a native push
//            callback (Dart_PostCObject) for true zero-latency updates
//          - No fade-in on play() to avoid click/pop artifacts
//          - No loop mode or crossfade support
// Known issues: Timer.periodic continues briefly after dispose() if a tick
//               is already scheduled in the event loop. The unused `paused`
//               variable in _startPolling should be removed.
// ---------------------------------------------------------------------------

import 'dart:async';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface;

/// Playback state for a [TrackPlayer].
///
/// `stopped` — no active playback, position is zero.
/// `playing` — actively decoding and outputting audio.
/// `paused` — playback suspended, position preserved.
enum PlaybackState { stopped, playing, paused }

/// High-level API for controlling a single audio track.
///
/// Each [TrackPlayer] maps to a native track slot (0-3).  Provides
/// [onStateChanged] and [onPositionChanged] streams for reactive UIs.
///
/// Internal [Timer.periodic] at 250ms polls the native engine for position
/// and state changes. The timer is active while [state] is [PlaybackState.playing].
class TrackPlayer {
  final int index;
  final FfiInterface _ffi = FfiInterface.instance;

  Timer? _timer;
  PlaybackState _state = PlaybackState.stopped;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  double _volume = 1.0;
  double _pan = 0.0;
  bool _mute = false;
  bool _solo = false;
  bool _loop = false;

  final StreamController<PlaybackState> _stateCtrl =
      StreamController<PlaybackState>.broadcast();
  final StreamController<Duration> _posCtrl =
      StreamController<Duration>.broadcast();

  Stream<PlaybackState> get onStateChanged => _stateCtrl.stream;
  Stream<Duration> get onPositionChanged => _posCtrl.stream;

  TrackPlayer(this.index);

  /// Current playback state: [PlaybackState.stopped], .playing, or .paused.
  PlaybackState get state => _state;

  /// Current playback position. Updated every 250ms via polling timer.
  Duration get position => _position;

  /// Total duration of the loaded audio. Updated when state becomes playing.
  Duration get duration => _duration;

  /// Per-track volume. Clamped to 0.0–1.0. Propagated to native engine.
  double get volume => _volume;

  /// Per-track pan. -1.0 = full left, 0.0 = center, 1.0 = full right.
  double get pan => _pan;

  /// Whether this track is muted (silenced in the mix).
  bool get mute => _mute;

  /// Whether this track is soloed (only soloed tracks play).
  bool get solo => _solo;

  /// Whether this track loops (repeats from beginning when finished).
  bool get loop => _loop;

  /// Sets per-track volume. Clamped to 0.0–1.0.
  set volume(double v) {
    _volume = v.clamp(0.0, 1.0);
    _ffi.trackSetVolume(index, _volume);
  }

  /// Sets per-track pan. Clamped to -1.0–1.0.
  set pan(double p) {
    _pan = p.clamp(-1.0, 1.0);
    _ffi.trackSetPan(index, _pan);
  }

  /// Mutes or unmutes this track.
  set mute(bool v) {
    _mute = v;
    _ffi.trackSetMute(index, v ? 1 : 0);
  }

  /// Enables or disables solo for this track.
  set solo(bool v) {
    _solo = v;
    _ffi.trackSetSolo(index, v ? 1 : 0);
  }

  /// Enables or disables loop for this track.
  set loop(bool v) {
    _loop = v;
    _ffi.trackSetLoop(index, v ? 1 : 0);
  }

  /// Starts playback of [path] on this track slot.
  ///
  /// Stops any existing playback first. Returns 0 on success, negative on error.
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

  /// Stops playback and resets position to zero. Cancels polling timer.
  void stop() {
    _timer?.cancel();
    _timer = null;
    _ffi.trackStop(index);
    _state = PlaybackState.stopped;
    _position = Duration.zero;
    _stateCtrl.add(_state);
    _posCtrl.add(_position);
  }

  /// Pauses playback. Position is preserved.
  void pause() {
    _ffi.trackPause(index);
    _state = PlaybackState.paused;
    _stateCtrl.add(_state);
  }

  /// Resumes from paused state.
  void resume() {
    _ffi.trackResume(index);
    _state = PlaybackState.playing;
    _stateCtrl.add(_state);
  }

  /// Seeks to [position]. The native engine seeks on the next decoder cycle.
  void seek(Duration position) {
    _ffi.trackSeek(index, position.inMilliseconds);
    _position = position;
    _posCtrl.add(_position);
  }

  /// Stops playback and releases stream controllers.
  ///
  /// After calling [dispose], the [TrackPlayer] should not be used again.
  void dispose() {
    stop();
    _stateCtrl.close();
    _posCtrl.close();
  }

  void _startPolling() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      final playing = _ffi.trackIsPlaying(index) != 0;

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
