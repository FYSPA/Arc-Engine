import 'dart:async';
import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface, FlacInfo;
import 'track_player.dart';
import 'pcm_stream.dart';
import 'audio_focus.dart' show AudioFocus, AudioFocusEvent;

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

  // ─── Audio Focus state ────────────────────────────────────────────────
  static bool _audioFocusEnabled = true;
  static bool _pauseOnNotification = true;
  static final List<int> _focusPausedTracks = [];
  static bool _wasDucked = false;
  static double _savedMasterVolume = 1.0;
  static StreamSubscription<AudioFocusEvent>? _focusSub;

  AudioEngine._()
      : tracks = List.unmodifiable(
          List.generate(4, (i) => TrackPlayer(i)),
        ) {
    _initAudioFocus();
    FfiInterface.instance.engineSetCrossfadeMs(_crossfadeMs.round());
  }

  void _initAudioFocus() {
    _focusSub = AudioFocus.events.listen(_onAudioFocusEvent);

    for (final track in tracks) {
      track.onStateChanged.listen((_) {
        if (_audioFocusEnabled) _updateAudioFocus();
      });
    }
  }

  void _updateAudioFocus() {
    final anyPlaying = tracks.any((t) => t.state == PlaybackState.playing);
    if (anyPlaying) {
      AudioFocus.request();
    } else {
      AudioFocus.abandon();
    }
  }

  void _onAudioFocusEvent(AudioFocusEvent event) {
    if (!_audioFocusEnabled) return;

    switch (event) {
      case AudioFocusEvent.gain:
        for (final idx in _focusPausedTracks) {
          if (idx >= 0 && idx < tracks.length) {
            tracks[idx].resume();
          }
        }
        _focusPausedTracks.clear();

        if (_wasDucked) {
          masterVolume = _savedMasterVolume;
          _wasDucked = false;
        }

      case AudioFocusEvent.loss:
        // Permanent loss (e.g. phone call, other app started).
        // Pause all and never auto-resume until user acts.
        _focusPausedTracks.clear();
        for (final track in tracks) {
          if (track.state == PlaybackState.playing) {
            track.pause();
          }
        }

      case AudioFocusEvent.lossTransient:
        // Temporary loss (notification, navigation prompt).
        if (_pauseOnNotification) {
          for (final track in tracks) {
            if (track.state == PlaybackState.playing) {
              _focusPausedTracks.add(track.index);
              track.pause();
            }
          }
        }

      case AudioFocusEvent.duck:
        if (!_wasDucked) {
          _savedMasterVolume = masterVolume;
          masterVolume = _savedMasterVolume * 0.3;
          _wasDucked = true;
        }
    }
  }

  /// Master output volume. 0.0 = silent, 1.0 = full. Clamped to 0.0–1.0.
  double get masterVolume => _masterVol;
  double _masterVol = 1.0;
  set masterVolume(double v) {
    _masterVol = v.clamp(0.0, 1.0);
    _ffi.mixerSetMasterVolume(_masterVol);
  }

  /// Crossfade duration in milliseconds between gapless tracks.
  /// 0 = off, max ~500ms (limited by MAX_CROSSFADE_FRAMES at 48kHz).
  static double get crossfadeMs => _crossfadeMs;
  static double _crossfadeMs = 80.0;
  static set crossfadeMs(double v) {
    _crossfadeMs = v.clamp(0.0, 500.0);
    FfiInterface.instance.engineSetCrossfadeMs(_crossfadeMs.round());
  }

  Stream<List<double>> startPcmStream({
    Duration interval = const Duration(milliseconds: 50),
  }) =>
      _pcmStream.start(interval: interval);

  void stopPcmStream() => _pcmStream.stop();

  // ─── Audio Focus configuration ────────────────────────────────────────

  /// Master switch for automatic audio focus handling.
  ///
  /// When enabled (default), the engine automatically requests audio focus
  /// when a track starts playing and abandons it when all tracks stop.
  /// Focus loss events are handled:
  /// - Phone calls / permanent loss → pause all tracks (no auto-resume)
  /// - Notifications (transient loss) → pause if [pauseOnNotification]
  /// - Ducking → lower master volume to 30%, restore on gain
  static bool get audioFocusEnabled => _audioFocusEnabled;
  static set audioFocusEnabled(bool v) {
    _audioFocusEnabled = v;
    if (!v) {
      AudioFocus.abandon();
      if (_wasDucked) {
        instance.masterVolume = _savedMasterVolume;
        _wasDucked = false;
      }
    }
  }

  /// Whether to pause playback on transient focus loss (notifications).
  ///
  /// When `true` (default), the engine pauses all playing tracks when a
  /// notification or other transient sound interrupts.  When `false`,
  /// notifications are ignored (audio continues playing).
  ///
  /// Phone calls always pause regardless of this setting.
  static bool get pauseOnNotification => _pauseOnNotification;
  static set pauseOnNotification(bool v) {
    _pauseOnNotification = v;
    AudioFocus.setPauseOnNotification(v);
  }

  /// Stream of raw [AudioFocusEvent]s from the Android AudioManager.
  ///
  /// Use this if you need custom handling beyond the built-in auto-behavior:
  /// ```dart
  /// AudioEngine.onAudioFocusChange.listen((event) {
  ///   if (event == AudioFocusEvent.loss) showBanner('Audio paused');
  /// });
  /// ```
  static Stream<AudioFocusEvent> get onAudioFocusChange => AudioFocus.events;

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

  // ─── Compressor ─────────────────────────────────────────────────────
  static bool _compressorEnabled = false;
  static double _compressorThreshold = -12.0;
  static double _compressorRatio = 4.0;
  static double _compressorAttack = 5.0;
  static double _compressorRelease = 100.0;
  static double _compressorKnee = 3.0;
  static double _compressorMakeup = 0.0;

  static bool get compressorEnabled => _compressorEnabled;
  static double get compressorThreshold => _compressorThreshold;
  static double get compressorRatio => _compressorRatio;
  static double get compressorAttack => _compressorAttack;
  static double get compressorRelease => _compressorRelease;
  static double get compressorKnee => _compressorKnee;
  static double get compressorMakeup => _compressorMakeup;

  static set compressorEnabled(bool v) {
    _compressorEnabled = v;
    final name = 'compressor'.toNativeUtf8();
    try {
      if (v) {
        FfiInterface.instance.fxAdd(name);
      } else {
        FfiInterface.instance.fxSetEnabled(name, 0);
      }
    } finally {
      calloc.free(name);
    }
  }

  static set compressorThreshold(double db) {
    _compressorThreshold = db.clamp(-60.0, 0.0);
    FfiInterface.instance.compressorSetThreshold(_compressorThreshold);
  }

  static set compressorRatio(double r) {
    _compressorRatio = r.clamp(1.0, 20.0);
    FfiInterface.instance.compressorSetRatio(_compressorRatio);
  }

  static set compressorAttack(double ms) {
    _compressorAttack = ms.clamp(0.1, 100.0);
    FfiInterface.instance.compressorSetAttack(_compressorAttack);
  }

  static set compressorRelease(double ms) {
    _compressorRelease = ms.clamp(10.0, 1000.0);
    FfiInterface.instance.compressorSetRelease(_compressorRelease);
  }

  static set compressorKnee(double db) {
    _compressorKnee = db.clamp(0.0, 12.0);
    FfiInterface.instance.compressorSetKnee(_compressorKnee);
  }

  static set compressorMakeup(double db) {
    _compressorMakeup = db.clamp(0.0, 24.0);
    FfiInterface.instance.compressorSetMakeup(_compressorMakeup);
  }

  // ─── Reverb ─────────────────────────────────────────────────────────
  static bool _reverbEnabled = false;
  static double _reverbMix = 0.3;
  static double _reverbDecay = 2.0;
  static double _reverbRoomSize = 0.5;
  static double _reverbDamping = 0.5;
  static double _reverbPreDelay = 20.0;

  static bool get reverbEnabled => _reverbEnabled;
  static double get reverbMix => _reverbMix;
  static double get reverbDecay => _reverbDecay;
  static double get reverbRoomSize => _reverbRoomSize;
  static double get reverbDamping => _reverbDamping;
  static double get reverbPreDelay => _reverbPreDelay;

  static set reverbEnabled(bool v) {
    _reverbEnabled = v;
    final name = 'reverb'.toNativeUtf8();
    try {
      if (v) {
        FfiInterface.instance.fxAdd(name);
      } else {
        FfiInterface.instance.fxSetEnabled(name, 0);
      }
    } finally {
      calloc.free(name);
    }
  }

  static set reverbMix(double v) {
    _reverbMix = v.clamp(0.0, 1.0);
    FfiInterface.instance.reverbSetMix(_reverbMix);
  }

  static set reverbDecay(double v) {
    _reverbDecay = v.clamp(0.1, 10.0);
    FfiInterface.instance.reverbSetDecay(_reverbDecay);
  }

  static set reverbRoomSize(double v) {
    _reverbRoomSize = v.clamp(0.0, 1.0);
    FfiInterface.instance.reverbSetRoomSize(_reverbRoomSize);
  }

  static set reverbDamping(double v) {
    _reverbDamping = v.clamp(0.0, 1.0);
    FfiInterface.instance.reverbSetDamping(_reverbDamping);
  }

  static set reverbPreDelay(double ms) {
    _reverbPreDelay = ms.clamp(0.0, 200.0);
    FfiInterface.instance.reverbSetPreDelay(_reverbPreDelay);
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

  // ─── Limiter ─────────────────────────────────────────────────────────
  static bool _limiterEnabled = true;
  static double _limiterThreshold = -0.5;

  static bool get limiterEnabled => _limiterEnabled;
  static double get limiterThreshold => _limiterThreshold;

  static set limiterEnabled(bool v) {
    _limiterEnabled = v;
    FfiInterface.instance.limiterSetEnabled(v ? 1 : 0);
  }

  static set limiterThreshold(double db) {
    _limiterThreshold = db.clamp(-60.0, 0.0);
    FfiInterface.instance.limiterSetThreshold(_limiterThreshold);
  }
}
