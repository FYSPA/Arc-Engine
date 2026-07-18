/// Arc Audio Engine — API pública.
///
/// ## Uso básico (nueva API)
/// ```dart
/// final engine = AudioEngine.instance;
/// engine.tracks[0].play('/path/to/file.wav');
/// engine.tracks[0].onPositionChanged.listen((pos) => print(pos));
/// ```
///
/// ## Backward compat (legacy, opera sobre track 0)
/// ```dart
/// AudioEngine.startAudio('/path/to/file.wav');
/// AudioEngine.stop();
/// AudioEngine.isPlaying;
/// ```
library audio_engine;

export 'src/ffi_bindings.dart' show FlacInfo;
export 'src/track_player.dart' show TrackPlayer, PlaybackState;
export 'src/audio_mixer.dart' show AudioEngine;
