// ---------------------------------------------------------------------------
// File: track_player.dart
// Purpose: High-level Dart API for individual audio track playback. Wraps
//          native track_play/stop/pause/resume/seek calls into an observable
//          object with Stream-based state and position notifications.
// Importance: Core user-facing API — every audio track in the engine is
//             controlled through this class. Used by AudioEngine.tracks[i].
// Missing: - No automatic reconnection if native library unloads
//          - No fade-in on play() to avoid click/pop artifacts
//          - No loop mode or crossfade support
// Known issues: None
// ---------------------------------------------------------------------------

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface, FlacMetadata;
import 'flac_metadata.dart';

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
/// Position updates arrive via native push callbacks (Dart_NativePort) instead
/// of polling, giving sub-frame latency with minimal CPU overhead.
class TrackPlayer {
  final int index;
  final FfiInterface _ffi = FfiInterface.instance;

  PlaybackState _state = PlaybackState.stopped;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  double _volume = 1.0;
  double _pan = 0.0;
  bool _mute = false;
  bool _solo = false;
  bool _loop = false;
  String _currentName = '';
  String _nextName = '';
  int _lastGapLessVersion = 0;
  int _seekingUntilMs = 0;
  bool _wasPlaying = false;
  FlacMetadataData? _metadata;

  final StreamController<PlaybackState> _stateCtrl =
      StreamController<PlaybackState>.broadcast();
  final StreamController<Duration> _posCtrl =
      StreamController<Duration>.broadcast();
  final StreamController<String> _nameCtrl =
      StreamController<String>.broadcast();
  final StreamController<String> _abortCtrl =
      StreamController<String>.broadcast();

  Stream<PlaybackState> get onStateChanged => _stateCtrl.stream;
  Stream<Duration> get onPositionChanged => _posCtrl.stream;
  Stream<String> get onNameChanged => _nameCtrl.stream;

  /// Emits the name of the next track that failed to transition via gapless.
  ///
  /// Fired when the native engine aborts a gapless transition (e.g. format
  /// mismatch between current and next track). The [String] value is the
  /// filename of the track that was queued but could not play.
  Stream<String> get onGaplessAborted => _abortCtrl.stream;

  // ─── Static ReceivePort for native push callbacks ────────────────────
  static ReceivePort? _globalPort;
  static final Map<int, TrackPlayer> _instances = {};
  static bool _portRegistered = false;
  static bool _dartApiInitialized = false;

  TrackPlayer(this.index);

  /// Current playback state: [PlaybackState.stopped], .playing, or .paused.
  PlaybackState get state => _state;

  /// Current playback position. Updated via native push callbacks.
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

  /// Full FLAC metadata for the current track (null if not loaded or not FLAC).
  FlacMetadataData? get metadata => _metadata;

  /// Title from Vorbis Comments. Empty string if not available.
  String get title => _metadata?.title ?? '';

  /// Title without numeric prefixes (e.g. "01 - Song" → "Song").
  String get titleClean => _metadata?.titleClean ?? '';

  /// Artist from Vorbis Comments. Empty string if not available.
  String get artist => _metadata?.artist ?? '';

  /// Album from Vorbis Comments. Empty string if not available.
  String get album => _metadata?.album ?? '';

  /// ISRC from CUESHEET. Empty string if not available.
  String get isrc => _metadata?.isrc ?? '';

  /// Track number from Vorbis Comments. Null if not available.
  int? get trackNumber => _metadata?.trackNumber;

  /// Release year from Vorbis Comments. Null if not available.
  int? get year => _metadata?.year;

  /// Sample rate in Hz (e.g. 44100, 48000, 96000). 0 if not loaded.
  int get sampleRate => _metadata?.sampleRate ?? 0;

  /// Bit depth (16, 24, 32). 0 if not loaded.
  int get bitDepth => _metadata?.bitDepth ?? 0;

  /// Number of channels. 0 if not loaded.
  int get channels => _metadata?.channels ?? 0;

  /// Approximate bitrate in kbps. 0 if not loaded.
  int get bitrate => _metadata?.bitrate ?? 0;

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

  /// Sets the next track to play automatically when this track finishes.
  ///
  /// The transition is gap-less — no silence between tracks. Set to
  /// `null` or call [clearNextTrack] to remove the queued track.
  void setNextTrack(String? path, {String? name}) {
    if (path == null || path.isEmpty) {
      _ffi.trackClearNext(index);
      _nextName = '';
    } else {
      final pathPtr = path.toNativeUtf8();
      try {
        _ffi.trackSetNext(index, pathPtr);
        _nextName = name ?? path.split('/').last;
      } finally {
        calloc.free(pathPtr);
      }
    }
  }

  /// Clears the queued next track for this track slot.
  void clearNextTrack() {
    _ffi.trackClearNext(index);
  }

  /// Starts playback of [path] on this track slot.
  ///
  /// Stops any existing playback first. Returns 0 on success, negative on error.
  int play(String path) {
    stop();
    _loadMetadata(path);
    final pathPtr = path.toNativeUtf8();
    try {
      final result = _ffi.trackPlay(index, pathPtr);
      if (result == 0) {
        _state = PlaybackState.playing;
        _currentName = path.split('/').last;
        _lastGapLessVersion = _ffi.trackGetGapLessVersion(index);
        _duration = _metadata?.duration ??
            Duration(milliseconds: _ffi.trackGetDuration(index));
        _nameCtrl.add(_currentName);
        _stateCtrl.add(_state);
        _ensurePortRegistered();
        _instances[index] = this;
        _startGaplessPolling();
      }
      return result;
    } finally {
      calloc.free(pathPtr);
    }
  }

  void _loadMetadata(String path) {
    if (!path.toLowerCase().endsWith('.flac')) {
      _metadata = null;
      return;
    }
    final pathPtr = path.toNativeUtf8();
    final metaPtr = calloc<FlacMetadata>();
    try {
      final result = _ffi.getFlacMetadata(pathPtr, metaPtr);
      if (result != 0) {
        _metadata = null;
        return;
      }
      final m = metaPtr.ref;
      final t = arrayToString(m.title);
      final a = arrayToString(m.artist);
      final al = arrayToString(m.album);
      final isrc = arrayToString(m.isrc);
      _metadata = FlacMetadataData(
        sampleRate: m.sampleRate,
        bitDepth: m.bitDepth,
        channels: m.channels,
        totalSamples: m.totalSamples,
        bitrate: m.bitrate,
        title: t,
        titleClean: computeTitleClean(t),
        artist: a,
        album: al,
        isrc: isrc,
        trackNumber: m.trackNumber != 0 ? m.trackNumber : null,
        year: m.year != 0 ? m.year : null,
        duration: Duration(milliseconds: m.durationMs),
      );
    } finally {
      calloc.free(pathPtr);
      calloc.free(metaPtr);
    }
  }

  /// Stops playback and resets position to zero. Cancels polling timer.
  void stop() {
    _gaplessTimer?.cancel();
    _gaplessTimer = null;
    _ffi.trackStop(index);
    _state = PlaybackState.stopped;
    _position = Duration.zero;
    _instances.remove(index);
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
    _seekingUntilMs = DateTime.now().millisecondsSinceEpoch + 1000;
  }

  Timer? _pcmTimer;
  StreamController<List<double>>? _pcmStreamCtrl;

  /// Starts emitting PCM samples from this track's ring buffer.
  ///
  /// Returns a broadcast [Stream] of [List<double>] containing interleaved
  /// float samples (-1.0 to 1.0). Call [stopPcmStream] to stop.
  Stream<List<double>> startPcmStream({
    Duration interval = const Duration(milliseconds: 50),
  }) {
    stopPcmStream();
    final ctrl = StreamController<List<double>>.broadcast();
    _pcmStreamCtrl = ctrl;
    _pcmTimer = Timer.periodic(interval, (_) {
      final available = _ffi.trackGetPcmAvailable(index);
      if (available <= 0) return;
      final frames = available > 512 ? 512 : available;
      final buffer = calloc<Float>(frames * 2);
      final samplesRead = _ffi.trackReadPcmSamples(index, buffer, frames);
      if (samplesRead > 0) {
        final count = samplesRead < 1024 ? samplesRead : 1024;
        final samples =
            List<double>.generate(count, (i) => buffer[i].toDouble());
        ctrl.add(samples);
      }
      calloc.free(buffer);
    });
    return ctrl.stream;
  }

  /// Stops the PCM stream and releases resources.
  void stopPcmStream() {
    _pcmTimer?.cancel();
    _pcmTimer = null;
    _pcmStreamCtrl?.close();
    _pcmStreamCtrl = null;
  }

  /// Analyzes the audio file and returns peak amplitude per bar for waveform visualization.
  /// Synchronous FFI call (~50-200ms). Returns empty list if unsupported or fails.
  List<double> analyzeWaveform({int numBars = 500}) {
    final buffer = calloc<Float>(numBars);
    try {
      final result = _ffi.trackAnalyzeWaveform(index, numBars, buffer);
      if (result != 0) return [];
      return List<double>.generate(numBars, (i) => buffer[i]);
    } finally {
      calloc.free(buffer);
    }
  }

  /// Stops playback and releases stream controllers.
  ///
  /// After calling [dispose], the [TrackPlayer] should not be used again.
  void dispose() {
    stopPcmStream();
    stop();
    _stateCtrl.close();
    _posCtrl.close();
    _nameCtrl.close();
    _abortCtrl.close();
  }

  // ─── Static ReceivePort for native push callbacks ──────────────────

  static void _ensurePortRegistered() {
    if (_globalPort != null) return;

    // Initialize Dart DL API (only once)
    if (!_dartApiInitialized) {
      FfiInterface.instance.trackInitDartApiDl(NativeApi.initializeApiDLData);
      _dartApiInitialized = true;
    }

    _globalPort = ReceivePort();
    _globalPort!.listen(_onNativeMessage);
    FfiInterface.instance
        .trackRegisterCallback(_globalPort!.sendPort.nativePort);
    _portRegistered = true;
  }

  static void _onNativeMessage(dynamic message) {
    if (message is! List || message.length != 3) return;
    final int trackIndex = message[0] as int;
    final int posMs = message[1] as int;
    final bool running = message[2] as bool;

    final player = _instances[trackIndex];
    if (player == null) return;

    player._handleNativeUpdate(posMs, running);
  }

  void _handleNativeUpdate(int posMs, bool running) {
    final newPos = Duration(milliseconds: posMs);

    if (running) {
      if (_state != PlaybackState.playing) {
        _state = PlaybackState.playing;
        _stateCtrl.add(_state);
      }
      // Suppress position updates during seek window to prevent slider jump-back
      final now = DateTime.now().millisecondsSinceEpoch;
      if (now >= _seekingUntilMs) {
        if (newPos != _position) {
          _position = newPos;
          _posCtrl.add(_position);
        }
      }
    } else if (_state == PlaybackState.playing) {
      _state = PlaybackState.stopped;
      _position = Duration.zero;
      _instances.remove(index);
      _gaplessTimer?.cancel();
      _gaplessTimer = null;
      _stateCtrl.add(_state);
      _posCtrl.add(_position);
    }
  }

  // ─── Gapless detection (lightweight 250ms poll) ────────────────────
  // Only polls gaplessVersion + gaplessAbort — 2 FFI calls instead of 5.

  Timer? _gaplessTimer;

  void _startGaplessPolling() {
    _gaplessTimer?.cancel();
    _wasPlaying = true;
    _gaplessTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      final curVersion = _ffi.trackGetGapLessVersion(index);
      if (curVersion != _lastGapLessVersion) {
        _lastGapLessVersion = curVersion;
        if (_ffi.trackGetGapLessAbort(index) != 0) {
          if (_nextName.isNotEmpty) {
            _abortCtrl.add(_nextName);
          }
          _nextName = '';
        } else if (_nextName.isNotEmpty) {
          _currentName = _nextName;
          _nextName = '';
          _nameCtrl.add(_currentName);
          // Update duration from native (totalFrames is updated during gapless)
          final nativeDurMs = _ffi.trackGetDuration(index);
          if (nativeDurMs > 0) {
            _duration = Duration(milliseconds: nativeDurMs);
          }
        }
      }
      // Detect song end (native push may not arrive for all formats)
      if (_wasPlaying && _ffi.trackIsPlaying(index) == 0) {
        _wasPlaying = false;
        _state = PlaybackState.stopped;
        _position = Duration.zero;
        _instances.remove(index);
        _gaplessTimer?.cancel();
        _gaplessTimer = null;
        _stateCtrl.add(_state);
        _posCtrl.add(_position);
      }
    });
  }
}
