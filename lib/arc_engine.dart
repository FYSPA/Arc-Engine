// ---------------------------------------------------------------------------
// File: arc_engine.dart
// Purpose: Library barrel file for the arc_engine plugin. Re-exports all
//          public API classes: AudioEngine, TrackPlayer, PlaybackState,
//          FlacInfo.
// Importance: Every consumer imports this single file to access the full API.
// Missing: - Should also export FfiInterface for advanced users who need
//            custom mock implementations or native binding access
// Known issues: None
// ---------------------------------------------------------------------------

/// Arc Audio Engine — Public API.
///
/// ## Basic usage (new API)
/// ```dart
/// final engine = AudioEngine.instance;
/// engine.tracks[0].play('/path/to/file.wav');
/// engine.tracks[0].onPositionChanged.listen((pos) => print(pos));
/// ```
///
/// ## Backward compat (legacy, operates on track 0)
/// ```dart
/// AudioEngine.startAudio('/path/to/file.wav');
/// AudioEngine.stop();
/// AudioEngine.isPlaying;
/// ```
library arc_engine;

export 'src/ffi_bindings.dart' show FlacInfo;
export 'src/track_player.dart' show TrackPlayer, PlaybackState;
export 'src/audio_mixer.dart' show AudioEngine;
