# Arc Audio Engine (AAE) — API Reference

> Multi-format audio playback engine for Flutter (Android). 4 simultaneous tracks, real-time DSP, crossfade, gapless transitions.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [AudioEngine (Singleton)](#2-audioengine-singleton)
3. [TrackPlayer](#3-trackplayer)
4. [Enums](#4-enums)
5. [Data Classes](#5-data-classes)
6. [MediaSession / Notifications](#6-mediasession--notifications)
7. [Constants & Ranges](#7-constants--ranges)
8. [Architecture](#8-architecture)

---

## 1. Quick Start

### Minimal playback

```dart
import 'package:arc_engine/arc_engine.dart';

final player = AudioEngine.instance.tracks[0];
player.play('/path/to/song.flac');
```

### Multi-track with volume/pan

```dart
final tracks = AudioEngine.instance.tracks;

tracks[0].play('/path/to/drums.wav');
tracks[0].volume = 0.8;
tracks[0].pan = -0.3; // left

tracks[1].play('/path/to/bass.mp3');
tracks[1].volume = 0.6;
tracks[1].pan = 0.3; // right
```

### EQ + Crossfade

```dart
AudioEngine.crossfadeMs = 3000.0; // 3-second crossfade

// Boost bass globally
AudioEngine.setEqBand(0, AudioEngine.eqLowShelf, 62.0, 6.0, 0.707);
AudioEngine.setEqBandEnabled(0, true);

// Or per-track override
AudioEngine.setTrackEqBand(0, 0, AudioEngine.eqLowShelf, 8.0, 0.5);
```

---

## 2. AudioEngine (Singleton)

Central manager. All configuration is static; per-track access via `AudioEngine.instance.tracks[i]`.

### 2.1 Properties

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `crossfadeMs` | `double` | `0.0–10000.0` | `0.0` | Crossfade duration in milliseconds. 0 = disabled. Persisted via SharedPreferences. |
| `crossfadeVolume` | `double` | `0.0–1.0` | `1.0` | Volume multiplier applied to the incoming track during crossfade. Persisted. |
| `audioFocusEnabled` | `bool` | — | `true` | Master switch for Android audio focus handling (ducking, becoming-noisy). |
| `pauseOnNotification` | `bool` | — | `true` | Whether to pause playback when a transient audio focus loss occurs (e.g., notification). |
| `masterVolume` | `double` | `0.0–1.0` | `1.0` | Master output volume applied after all track mixing. |

```dart
AudioEngine.crossfadeMs = 5000.0;
AudioEngine.masterVolume = 0.9;
AudioEngine.audioFocusEnabled = true;
AudioEngine.pauseOnNotification = false;
```

### 2.2 Playback (Legacy Static API)

Convenience methods for simple single-track use cases. **Scope is asymmetric** — some methods affect all tracks, others only track 0:

| Method | Scope | Returns | Description |
|---|---|---|---|
| `startAudio` | track 0 | `0` = success | Play a local file (auto-detect format). |
| `streamUrl` | track 0 | `0` = success | Stream from URL. Falls back to download-then-play if native streaming fails or doesn't start within 500ms. |
| `stop` | **ALL tracks** | — | Stop all playing tracks. |
| `pause` | **ALL tracks** | — | Pause all playing tracks. |
| `resume` | **ALL tracks** | — | Resume all paused tracks. |
| `seek` | track 0 | `0` = success | Seek to position in milliseconds. |
| `getPosition` | track 0 | `int` (ms) | Current playback position. |
| `getDuration` | track 0 | `int` (ms) | Total duration of loaded audio. |
| `isPlaying` | track 0 | `bool` | Whether currently playing. |
| `getPcmAvailable` | mixed output | `int` (frames) | PCM frames available in the mixed output ring buffer. |
| `readPcmSamples` | mixed output | `int` (frames) | **Advanced.** Read raw interleaved floats via `Pointer<Float>`. Prefer `startPcmStream()`. |

> **Note:** `stop()`/`pause()`/`resume()` affect all 4 tracks. `seek()`/`getPosition()`/`getDuration()`/`isPlaying` only operate on track 0. For multi-track control, use `AudioEngine.instance.tracks[i]` directly.

```dart
final rc = AudioEngine.startAudio('/path/to/song.flac');
if (rc == 0) {
  print('Playing! Duration: ${AudioEngine.getDuration()}ms');
}
```

### 2.3 Multi-Track Access

```dart
AudioEngine.instance.tracks // List<TrackPlayer> (length 4, unmodifiable)
```

The engine supports exactly 4 simultaneous tracks (slots 0–3). Each `TrackPlayer` is independent with its own volume, pan, mute, solo, loop, and metadata.

```dart
final t0 = AudioEngine.instance.tracks[0];
final t1 = AudioEngine.instance.tracks[1];
```

### 2.4 EQ — Global

10-band parametric equalizer applied to the master mix. Each band has frequency, gain, Q, filter type, and enable flag.

| Method | Signature | Description |
|---|---|---|
| `setEqBand` | `static void setEqBand(int index, int type, double freq, double gain, double q)` | Set a global EQ band (index 0–9). |
| `setEqBandEnabled` | `static void setEqBandEnabled(int index, bool enabled)` | Enable/disable a specific band. |
| `setEqBypass` | `static void setEqBypass(bool bypass)` | Bypass the entire global EQ. |
| `resetEq` | `static void resetEq()` | Reset all 10 bands to flat (0 dB gain, peaking type, Q 0.707). |

**Band frequencies (fixed):**

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Freq | 31 Hz | 62 Hz | 125 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 8 kHz | 16 kHz |

```dart
// Boost bass at 62 Hz by 6 dB
AudioEngine.setEqBand(1, AudioEngine.eqLowShelf, 62.0, 6.0, 0.707);
AudioEngine.setEqBandEnabled(1, true);

// Bypass entire EQ
AudioEngine.setEqBypass(true);
```

### 2.5 EQ — Per-Track

Each track can have its own `DspProcessor` that overrides the global EQ. When a per-track override exists, it completely replaces the global EQ for that track.

| Method | Signature | Description |
|---|---|---|
| `setTrackEqBand` | `static void setTrackEqBand(int trackIndex, int bandIndex, int type, double gain, double q, {double? freq})` | Set a per-track EQ band. Auto-creates the override. Pass `null` for `freq` to use the standard band frequency. |
| `setTrackEqBandEnabled` | `static void setTrackEqBandEnabled(int trackIndex, int bandIndex, bool enabled)` | Enable/disable a per-track band. |
| `setTrackEqBypass` | `static void setTrackEqBypass(int trackIndex, bool bypass)` | Bypass per-track EQ (keeps override alive). |
| `resetTrackEq` | `static void resetTrackEq(int trackIndex)` | Reset per-track EQ to flat (keeps override active). |
| `clearTrackEq` | `static void clearTrackEq(int trackIndex)` | Remove per-track override entirely (track reverts to global EQ). |
| `getTrackEqState` | `static EqTrackState? getTrackEqState(int trackIndex)` | Get the per-track EQ state, or `null` if using global. |

```dart
// Apply vocal boost to track 0 only
AudioEngine.setTrackEqBand(0, 4, AudioEngine.eqPeaking, 3.0, 1.0);
AudioEngine.setTrackEqBand(0, 5, AudioEngine.eqPeaking, 4.0, 1.0);
AudioEngine.setTrackEqBandEnabled(0, 4, true);
AudioEngine.setTrackEqBandEnabled(0, 5, true);

// Remove per-track override (revert to global)
AudioEngine.clearTrackEq(0);
```

### 2.6 Compressor

Post-EQ dynamics compressor applied to the master mix.

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `compressorEnabled` | `bool` | — | `false` | Enable/disable the compressor. |
| `compressorThreshold` | `double` | `-60.0–0.0` dB | `-20.0` | Level above which compression starts. |
| `compressorRatio` | `double` | `1.0–20.0` | `4.0` | Compression ratio (4:1 = 4 dB in → 1 dB out). |
| `compressorAttack` | `double` | `0.1–100.0` ms | `10.0` | Time to reach full compression after threshold crossed. |
| `compressorRelease` | `double` | `10.0–1000.0` ms | `100.0` | Time to release compression after signal drops below threshold. |
| `compressorKnee` | `double` | `0.0–12.0` dB | `6.0` | Soft-knee width for smoother transitions. |
| `compressorMakeup` | `double` | `0.0–24.0` dB | `0.0` | Output gain to compensate for level reduction. |

```dart
AudioEngine.compressorEnabled = true;
AudioEngine.compressorThreshold = -18.0;
AudioEngine.compressorRatio = 4.0;
AudioEngine.compressorAttack = 10.0;
AudioEngine.compressorRelease = 150.0;
AudioEngine.compressorKnee = 6.0;
AudioEngine.compressorMakeup = 3.0;
```

### 2.7 Reverb

Algorithmic reverb with 4 comb + 2 all-pass filters.

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `reverbEnabled` | `bool` | — | `false` | Enable/disable reverb. |
| `reverbMix` | `double` | `0.0–1.0` | `0.3` | Wet/dry mix. 0.0 = fully dry, 1.0 = fully wet. |
| `reverbDecay` | `double` | `0.1–10.0` s | `2.0` | Reverb tail decay time in seconds. |
| `reverbRoomSize` | `double` | `0.0–1.0` | `0.5` | Simulated room size. |
| `reverbDamping` | `double` | `0.0–1.0` | `0.5` | High-frequency damping factor. |
| `reverbPreDelay` | `double` | `0.0–200.0` ms | `20.0` | Pre-delay before reverb onset. |

```dart
AudioEngine.reverbEnabled = true;
AudioEngine.reverbMix = 0.4;
AudioEngine.reverbDecay = 3.0;
AudioEngine.reverbRoomSize = 0.6;
```

### 2.8 Limiter

Post-mix limiter with envelope follower and look-ahead buffer. Prevents clipping.

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `limiterEnabled` | `bool` | — | `true` | Enable/disable limiter. |
| `limiterThreshold` | `double` | `-60.0–0.0` dB | `-1.0` | Maximum peak level allowed at output. |
| `limiterAttack` | `double` | `0.1–100.0` ms | `5.0` | Time to engage full limiting. |
| `limiterRelease` | `double` | `10.0–1000.0` ms | `100.0` | Time to release limiting. |
| `limiterLookahead` | `double` | `0.0–20.0` ms | `5.0` | Look-ahead buffer for transient detection. 0 = no look-ahead. |

```dart
AudioEngine.limiterEnabled = true;
AudioEngine.limiterThreshold = -3.0;
AudioEngine.limiterLookahead = 10.0;
```

### 2.9 Audio Focus

| Member | Type | Description |
|---|---|---|
| `audioFocusEnabled` | `bool` | Master switch for audio focus handling. |
| `pauseOnNotification` | `bool` | Pause on transient focus loss. |
| `onAudioFocusChange` | `Stream<AudioFocusEvent>` | Raw stream of focus events from Android AudioManager. |

```dart
AudioEngine.onAudioFocusChange.listen((event) {
  switch (event) {
    case AudioFocusEvent.gain:       // Resumed focus
    case AudioFocusEvent.loss:       // Lost permanently
    case AudioFocusEvent.lossTransient: // Temporarily lost (call, notification)
    case AudioFocusEvent.duck:       // Another app plays over us
    case AudioFocusEvent.becomingNoisy: // Headphones disconnected
  }
});
```

### 2.10 PCM Stream (Global)

Real-time access to the mixed audio output for visualization (VU meter, oscilloscope).

| Method | Signature | Description |
|---|---|---|
| `startPcmStream` | `Stream<List<double>> startPcmStream({Duration interval})` | Start emitting interleaved float samples (-1.0 to 1.0) at the given interval. Default: 50ms. |
| `stopPcmStream` | `void stopPcmStream()` | Stop the stream. |

```dart
final stream = AudioEngine.instance.startPcmStream(
  interval: const Duration(milliseconds: 30),
);
stream.listen((samples) {
  // samples: List<double>, interleaved stereo floats
  final leftPeak = samples.where((_, i) => i.isEven).reduce(max);
  final rightPeak = samples.where((_, i) => i.isOdd).reduce(max);
});
```

### 2.11 Export / Convert

| Method | Signature | Returns | Description |
|---|---|---|---|
| `exportToWav` | `static Future<int> exportToWav(String outputPath, {int sampleRate, int bitDepth})` | `0` = success | Offline mix to WAV. Re-decodes each track from original files. Reads live volume/pan/mute/solo state but does **not** pause tracks or capture real-time automation. |
| `convertFileToWav` | `static Future<int> convertFileToWav(String inputPath, String outputPath, {int sampleRate, int bitDepth})` | `0` = success | Convert a single file to WAV (no effects applied). |

Default parameters: `sampleRate: 44100`, `bitDepth: 24`.

> **Note:** `exportToWav` performs an offline re-decode of each track from the original files on disk. It reads the current volume, pan, mute, and solo state at the moment of the call, but does not capture real-time automation (e.g., volume fades happening during playback). Tracks are **not** paused during export.

```dart
final rc = await AudioEngine.exportToWav(
  '/sdcard/Download/mix.wav',
  sampleRate: 44100,
  bitDepth: 24,
);
```

### 2.12 FLAC Metadata

| Method | Signature | Returns | Description |
|---|---|---|---|
| `getFlacInfo` | `static Future<Map<String, dynamic>> getFlacInfo(String path)` | Map with sampleRate, channels, bitsPerSample, totalSamples, durationMs | Read FLAC STREAMINFO. |
| `getFlacMetadata` | `static Future<FlacMetadataData?> getFlacMetadata(String path)` | `FlacMetadataData?` or `null` | Full metadata (tags + technical info). |
| `playFlac` | `static Future<void> playFlac(String path)` | — | Play a FLAC file directly. |
| `playWav` | `static Future<void> playWav(String path)` | — | Play a WAV file directly. |
| `playAudio` | `static Future<void> playAudio(String path)` | — | Auto-detect format and play. |

```dart
final info = await AudioEngine.getFlacInfo('/path/to/song.flac');
print('Duration: ${info['durationMs']}ms, SR: ${info['sampleRate']}Hz');

final meta = await AudioEngine.getFlacMetadata('/path/to/song.flac');
if (meta != null) {
  print('${meta.artist} — ${meta.title} (${meta.album})');
}
```

---

## 3. TrackPlayer

Per-track controller. Each instance is bound to a fixed slot index (0–3).

### 3.1 Constructor

```dart
TrackPlayer(int index)
```

| Parameter | Type | Description |
|---|---|---|
| `index` | `int` | Track slot index (0–3). Obtained via `AudioEngine.instance.tracks[i]`. |

### 3.2 Playback Methods

| Method | Signature | Returns | Description |
|---|---|---|---|
| `play` | `int play(String path)` | `0` = success | Start playback. **Always calls `stop()` first** — aborts any active gapless transition on this slot. |
| `stop` | `void stop()` | — | Stop playback and reset position to zero. |
| `pause` | `void pause()` | — | Pause playback (position preserved). |
| `resume` | `void resume()` | — | Resume from paused state. |
| `seek` | `void seek(Duration position)` | — | Seek to a specific position. |
| `dispose` | `void dispose()` | — | Stop playback, release all stream controllers. **Track should not be used after dispose.** |

```dart
final player = AudioEngine.instance.tracks[0];
player.play('/path/to/song.flac');
player.seek(const Duration(minutes: 1, seconds: 30));
player.pause();
player.resume();
player.stop();
```

### 3.3 Gapless Transitions

Queue a track to start automatically when the current track ends. No silence gap between tracks.

| Method | Signature | Description |
|---|---|---|
| `setNextTrack` | `void setNextTrack(String? path, {String? name})` | Queue the next file. Pass `null` to clear. `name` is optional display name. |
| `clearNextTrack` | `void clearNextTrack()` | Clear the queued next track. |

**Flow:**
1. Current track plays → engine detects approaching end
2. Pre-decodes the queued next track in a background thread
3. At the crossfade point (or at end if crossfade=0), switches to the new track
4. Fires `onNameChanged` with the new track's name
5. Your code should re-queue the next file in the list

```dart
final player = AudioEngine.instance.tracks[0];
player.play('/path/to/song1.flac');

// Queue song2 to play immediately after song1
player.setNextTrack('/path/to/song2.flac', name: 'Song 2');

// Listen for the transition
player.onNameChanged.listen((newName) {
  // Song2 is now playing — queue song3
  player.setNextTrack('/path/to/song3.flac', name: 'Song 3');
});
```

**Gapless abort:** If the queued track fails to load (format mismatch, file deleted, etc.), the engine fires `onGaplessAborted` with the failed filename. Your code should play it fresh:

```dart
player.onGaplessAborted.listen((abortedName) {
  // Queue failed — play the aborted track from scratch
  player.play('/path/to/$abortedName');
  player.setNextTrack('/path/to/next.flac');
});
```

**Aborting a gapless transition:** Calling `play()` on a track always calls `stop()` first, which unconditionally aborts any in-progress gapless transition (the decoder thread receives a stop signal via eventfd and exits). There is no race condition — the new playback starts cleanly from scratch.

### 3.4 Volume & Pan

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `volume` | `double` | `0.0–1.0` | `1.0` | Per-track volume. Atomic — no glitches on change. |
| `pan` | `double` | `-1.0–1.0` | `0.0` | Constant-power pan. `-1.0` = full left, `0.0` = center, `1.0` = full right. |

```dart
player.volume = 0.7;
player.pan = -0.5; // slightly left
```

### 3.5 Mute & Solo

| Property | Type | Default | Description |
|---|---|---|---|
| `mute` | `bool` | `false` | Mute this track (output silenced but decoding continues). |
| `solo` | `bool` | `false` | Solo this track. When any track is soloed, only soloed tracks are heard. |

**Priority:** Solo overrides mute. If track A is soloed and track B is muted, only track A plays. If no tracks are soloed, mute works normally.

```dart
player.mute = true;   // silence this track
player.solo = true;   // only this track (and other soloed tracks) heard
```

### 3.6 Loop / Repeat

| Property | Type | Default | Description |
|---|---|---|---|
| `repeatCount` | `int` | `0` | Loop count: `0` = off, `-1` = infinite, `N > 0` = repeat N times then stop. |
| `loop` | `bool` | `false` | Convenience getter/setter. `true` = `repeatCount = -1`, `false` = `repeatCount = 0`. |

**Behavior:**
- `repeatCount = 0` → play once, then stop
- `repeatCount = -1` → loop infinitely (seek to start on EOS)
- `repeatCount = N` → after N repetitions, **stop** (not gapless to next track)

**Spotify-style cycling:** OFF → infinite → 1 → OFF

```dart
player.repeatCount = -1;  // infinite loop
player.repeatCount = 3;   // repeat 3 times then stop
player.loop = true;       // same as repeatCount = -1
```

### 3.7 State Properties

| Property | Type | Description |
|---|---|---|
| `state` | `PlaybackState` | Current state: `stopped`, `playing`, or `paused`. |
| `position` | `Duration` | Current playback position. Updated via native push callback. |
| `duration` | `Duration` | Total duration of the loaded audio file. |
| `displayName` | `String` | Display name: metadata title if available, otherwise the filename. |
| `currentName` | `String` | Name of the currently playing track (filename or metadata title). |
| `nextName` | `String` | Name of the queued next track for gapless transition. Empty if none queued. |
| `index` | `int` | The track slot index (0–3). |
| `sampleRate` | `int` | Sample rate in Hz (0 if no track loaded). |
| `bitDepth` | `int` | Bit depth: 16, 24, or 32 (0 if no track loaded). |
| `channels` | `int` | Number of channels: 1 = mono, 2 = stereo (0 if no track loaded). |
| `bitrate` | `int` | Approximate bitrate in kbps (0 if unavailable). |

### 3.8 Metadata Properties

FLAC files expose Vorbis Comments and CUESHEET metadata. Other formats return empty strings / null.

| Property | Type | Description |
|---|---|---|
| `metadata` | `FlacMetadataData?` | Full metadata object (null if not FLAC or not loaded). |
| `title` | `String` | Title from Vorbis Comments. |
| `titleClean` | `String` | Title with numeric prefixes stripped (e.g., "01 - Song" → "Song"). |
| `artist` | `String` | Artist from Vorbis Comments. |
| `album` | `String` | Album from Vorbis Comments. |
| `isrc` | `String` | ISRC from CUESHEET (empty if unavailable). |
| `trackNumber` | `int?` | Track number (null if unavailable). |
| `year` | `int?` | Release year (null if unavailable). |

```dart
player.play('/path/to/flac-song.flac');
await Future.delayed(const Duration(milliseconds: 500));

if (player.metadata != null) {
  print('${player.artist} — ${player.title}');
  print('Album: ${player.album}, Year: ${player.year}');
}
```

### 3.9 Crossfade State

| Property | Type | Description |
|---|---|---|
| `isCrossfading` | `bool` | Whether a crossfade is currently active on this track. |
| `crossfadeRemaining` | `int` | Frames remaining in the current crossfade (0 if not crossfading). |
| `fadeLen` | `int` | Total crossfade length in frames. |

### 3.10 Streams

All streams are broadcast — multiple listeners allowed.

| Stream | Event Type | Description |
|---|---|---|
| `onStateChanged` | `Stream<PlaybackState>` | Emitted on every state change (stopped/playing/paused). |
| `onPositionChanged` | `Stream<Duration>` | Emitted on position updates (native push, ~250ms interval). |
| `onNameChanged` | `Stream<String>` | Emitted when the current track name changes (gapless transition completed). |
| `onGaplessAborted` | `Stream<String>` | Emits the filename of a track that failed a gapless transition. |

```dart
player.onStateChanged.listen((state) {
  if (state == PlaybackState.stopped) {
    print('Track finished');
  }
});

player.onPositionChanged.listen((pos) {
  updateSeekBar(pos.inMilliseconds / player.duration.inMilliseconds);
});

player.onNameChanged.listen((name) {
  print('Now playing: $name');
  reQueueNextTrack();
});
```

### 3.11 PCM Stream (Per-Track)

| Method | Signature | Description |
|---|---|---|
| `startPcmStream` | `Stream<List<double>> startPcmStream({Duration interval})` | Start per-track PCM stream. Interleaved floats (-1.0 to 1.0). Default interval: 50ms. |
| `stopPcmStream` | `void stopPcmStream()` | Stop the per-track PCM stream. |

### 3.12 Waveform Analysis

| Method | Signature | Returns | Description |
|---|---|---|---|
| `analyzeWaveform` | `List<double> analyzeWaveform({int numBars})` | `List<double>` (0.0–1.0 per bar) | **Synchronous** — reads from the audio file on disk (~50-200ms). Returns peak amplitude per bar. Default: 500 bars. Only works for FLAC and WAV. |

```dart
// Runs on the main thread (~50-200ms). For large files, consider
// running in a compute() isolate to avoid blocking the UI.
final peaks = player.analyzeWaveform(numBars: 200);
// peaks[0] = peak amplitude of first bar (0.0–1.0)
```

---

## 4. Enums

### PlaybackState

```dart
enum PlaybackState { stopped, playing, paused }
```

| Value | Description |
|---|---|
| `stopped` | No active playback. Position is zero. |
| `playing` | Actively decoding and outputting audio. |
| `paused` | Playback suspended. Position preserved. |

### AudioFocusEvent

```dart
enum AudioFocusEvent {
  gain,
  loss,
  lossTransient,
  duck,
  becomingNoisy,
}
```

| Value | When Fired |
|---|---|
| `gain` | Regained audio focus (e.g., after a call ends). |
| `loss` | Lost audio focus permanently (e.g., another app started). |
| `lossTransient` | Lost temporarily (e.g., incoming notification). |
| `duck` | Another app wants to play at reduced volume (e.g., navigation prompt). |
| `becomingNoisy` | Bluetooth/wired headphones disconnected. |

---

## 5. Data Classes

### FlacMetadataData

Returned by `AudioEngine.getFlacMetadata()`. Immutable.

```dart
const FlacMetadataData({
  required int sampleRate,    // Hz
  required int bitDepth,      // 16, 24, or 32
  required int channels,      // 1 = mono, 2 = stereo
  required int totalSamples,
  required int bitrate,       // kbps
  required String title,
  required String titleClean, // title with numeric prefixes stripped
  required String artist,
  required String album,
  required String isrc,       // from CUESHEET
  int? trackNumber,
  int? year,
  required Duration duration,
})
```

### EqTrackState

Per-track EQ override state. Mutable.

```dart
EqTrackState({
  List<double>? gains,     // 10 elements, default: all 0.0
  List<int>? types,        // 10 elements, default: all EqState.peaking
  List<double>? qs,        // 10 elements, default: all 0.707
  List<bool>? enabled,     // 10 elements, default: all false
  bool bypass = false,
})
```

| Method | Signature | Description |
|---|---|---|
| `apply` | `void apply(int trackIndex)` | Re-apply all 10 bands to native in bulk. **Not needed for normal use** — `setTrackEqBand()` pushes each change to native immediately. Only useful after bulk-mutating the state object directly. |
| `reset` | `void reset()` | Reset all gains to 0, types to peaking, Q to 0.707, enabled to false, bypass to false. |

> **Note:** Calling `AudioEngine.setTrackEqBand()` automatically pushes each band change to native via FFI. You do **not** need to call `apply()` manually unless you directly mutate the `EqTrackState` lists and want to re-apply all bands at once.

### EqPreset

Named EQ preset with gains, types, and Q values for all 10 bands.

```dart
const EqPreset({
  required String name,
  required List<double> gains,   // 10 dB values
  List<int> types = const [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  List<double> qs = const [0.707, 0.707, 0.707, 0.707, 0.707,
                           0.707, 0.707, 0.707, 0.707, 0.707],
  bool isBuiltIn = true,
})
```

**Built-in presets** (10):

| Preset | Name | Notable Gains / Types |
|---|---|---|
| `EqPreset.flat` | Flat | All 0 dB, all peaking |
| `EqPreset.rock` | Rock | `[5,3,0,-1,-2,0,2,3,4,3]` |
| `EqPreset.pop` | Pop | `[-1,0,1,3,4,3,1,0,-1,-1]` |
| `EqPreset.jazz` | Jazz | `[3,3,2,1,0,0,1,2,2,1]` |
| `EqPreset.classical` | Classical | `[4,3,1,0,0,0,0,1,3,4]` |
| `EqPreset.bassBoost` | Bass Boost | `[8,6,4,2,0,...]`, lowShelf on bands 0-1 |
| `EqPreset.trebleBoost` | Treble Boost | `[0,0,0,0,0,0,2,4,6,8]`, highShelf on bands 8-9 |
| `EqPreset.vocal` | Vocal | `[-2,-1,0,2,4,4,3,1,0,-1]` |
| `EqPreset.electronic` | Electronic | Mixed types, custom Qs |
| `EqPreset.acoustic` | Acoustic | `[2,3,3,1,0,1,2,3,2,1]` |

**Serialization:**

```dart
final json = preset.toJson();
final restored = EqPreset.fromJson(json);

final jsonStr = EqPreset.listToJson(presets);
final list = EqPreset.listFromJson(jsonStr);
```

### StaticWaveformWidget

Stateless widget for rendering a static waveform with seek support.

```dart
const StaticWaveformWidget({
  super.key,
  required List<double> peaks,        // 0.0–1.0 per bar
  double position = 0.0,              // playhead 0.0–1.0 (fraction of width)
  Color color = const Color(0xFF7C4DFF),           // unplayed bar color
  Color playedColor = const Color(0xFF7C4DFF),     // played bar color
  double height = 48,                 // widget height in logical pixels
  ValueChanged<double>? onSeek,       // callback on tap/drag (0.0–1.0)
})
```

```dart
StaticWaveformWidget(
  peaks: player.analyzeWaveform(numBars: 200),
  position: player.position.inMilliseconds / player.duration.inMilliseconds,
  color: Colors.grey.shade700,
  playedColor: Colors.deepPurple,
  height: 64,
  onSeek: (fraction) {
    final ms = (fraction * player.duration.inMilliseconds).toInt();
    player.seek(Duration(milliseconds: ms));
  },
)
```

---

## 6. MediaSession / Notifications

All members are static. Provides Android lock screen and notification media controls.

### Methods

| Method | Signature | Description |
|---|---|---|
| `requestPermission` | `static Future<bool> requestPermission()` | Request notification permission (Android 13+). Returns true if granted. |
| `ensureService` | `static Future<void> ensureService()` | Pre-start the notification service. |
| `setMetadata` | `static Future<void> setMetadata({required String title, required String artist, required String album, required int durationMs})` | Update lock screen / notification metadata. |
| `setPlaybackState` | `static Future<void> setPlaybackState({required bool isPlaying, required int positionMs, double speed})` | Update playback state on the notification. |
| `setArtwork` | `static Future<void> setArtwork(String path)` | Set album artwork from URL or local path. |
| `show` | `static Future<void> show({required String title, required String artist, required bool isPlaying})` | Show persistent notification. |
| `hide` | `static Future<void> hide()` | Hide and dismiss notification. |

### Streams

| Stream | Event Type | Description |
|---|---|---|
| `onCommand` | `Stream<MediaCommand>` | Commands from notification / lock screen controls. |

### MediaCommand (Sealed Class)

```dart
sealed class MediaCommand {
  const factory MediaCommand.play()              = MediaCommandPlay;
  const factory MediaCommand.pause()             = MediaCommandPause;
  const factory MediaCommand.next()              = MediaCommandNext;
  const factory MediaCommand.previous()          = MediaCommandPrevious;
  const factory MediaCommand.seekTo(int positionMs) = MediaCommandSeekTo;
  const factory MediaCommand.stop()              = MediaCommandStop;
}
```

| Subtype | Fields | Description |
|---|---|---|
| `MediaCommandPlay` | — | User pressed play. |
| `MediaCommandPause` | — | User pressed pause. |
| `MediaCommandNext` | — | User pressed next. |
| `MediaCommandPrevious` | — | User pressed previous. |
| `MediaCommandSeekTo` | `int positionMs` | User seeked to a position. |
| `MediaCommandStop` | — | User pressed stop. |

**Custom next/previous handlers:**

```dart
AudioEngine.instance.onNext = () {
  // Custom behavior: play next track in your playlist
  playNextInPlaylist();
};

AudioEngine.instance.onPrevious = () {
  // Custom behavior: play previous track
  playPreviousInPlaylist();
};
```

**Integration example:**

```dart
// Initialize
await MediaSession.requestPermission();
await MediaSession.ensureService();

// Update when track changes
MediaSession.setMetadata(
  title: player.title,
  artist: player.artist,
  album: player.album,
  durationMs: player.duration.inMilliseconds,
);
MediaSession.setPlaybackState(isPlaying: true, positionMs: player.position.inMilliseconds);
MediaSession.show(title: player.title, artist: player.artist, isPlaying: true);

// Handle commands
MediaSession.onCommand.listen((cmd) {
  switch (cmd) {
    case MediaCommandPlay():    player.resume();
    case MediaCommandPause():   player.pause();
    case MediaCommandStop():    player.stop();
    case MediaCommandNext():    playNext();
    case MediaCommandPrevious(): playPrevious();
    case MediaCommandSeekTo(:final positionMs):
      player.seek(Duration(milliseconds: positionMs));
  }
});
```

---

## 7. Constants & Ranges

### EQ Filter Types

| Constant | Value | Description |
|---|---|---|
| `AudioEngine.eqPeaking` | `0` | Standard peaking bell filter |
| `AudioEngine.eqLowShelf` | `1` | Low shelf (boost/cut bass) |
| `AudioEngine.eqHighShelf` | `2` | High shelf (boost/cut treble) |
| `AudioEngine.eqLowPass` | `3` | Low-pass filter (cut highs) |
| `AudioEngine.eqHighPass` | `4` | High-pass filter (cut lows) |

### Band Frequencies (Fixed)

```
[31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000] Hz
```

### Parameter Ranges Summary

| Parameter | Min | Max | Default | Unit |
|---|---|---|---|---|
| Volume (per-track) | 0.0 | 1.0 | 1.0 | — |
| Volume (master) | 0.0 | 1.0 | 1.0 | — |
| Pan | -1.0 | 1.0 | 0.0 | — |
| Crossfade duration | 0.0 | 10000.0 | 0.0 | ms |
| Crossfade volume | 0.0 | 1.0 | 1.0 | — |
| EQ gain | -12.0 | +12.0 | 0.0 | dB |
| EQ Q | 0.1 | 10.0 | 0.707 | — |
| Compressor threshold | -60.0 | 0.0 | -20.0 | dB |
| Compressor ratio | 1.0 | 20.0 | 4.0 | — |
| Compressor attack | 0.1 | 100.0 | 10.0 | ms |
| Compressor release | 10.0 | 1000.0 | 100.0 | ms |
| Compressor knee | 0.0 | 12.0 | 6.0 | dB |
| Compressor makeup | 0.0 | 24.0 | 0.0 | dB |
| Reverb mix | 0.0 | 1.0 | 0.3 | — |
| Reverb decay | 0.1 | 10.0 | 2.0 | s |
| Reverb room size | 0.0 | 1.0 | 0.5 | — |
| Reverb damping | 0.0 | 1.0 | 0.5 | — |
| Reverb pre-delay | 0.0 | 200.0 | 20.0 | ms |
| Limiter threshold | -60.0 | 0.0 | -1.0 | dB |
| Limiter attack | 0.1 | 100.0 | 5.0 | ms |
| Limiter release | 10.0 | 1000.0 | 100.0 | ms |
| Limiter lookahead | 0.0 | 20.0 | 5.0 | ms |
| Loop repeatCount | -1 | ∞ | 0 | — |

---

## 8. Architecture

### Data Flow

```
┌─────────────┐     ┌──────────┐     ┌──────────────────┐     ┌──────────────┐
│   Dart API  │────▶│  FFI     │────▶│  C++ Engine      │────▶│  AAudio      │
│  (Flutter)  │     │  Bridge  │     │                  │     │  Callback    │
│             │◀────│          │◀────│  4 Decoder       │◀────│  (real-time) │
│  AudioEngine│     │          │     │  Threads +       │     │              │
│  TrackPlayer│     │          │     │  Ring Buffer     │     │  Speaker/    │
│  EQ/FX/UI   │     │          │     │  (SPSC lock-free)│     │  Headphones  │
└─────────────┘     └──────────┘     └──────────────────┘     └──────────────┘
       │                                     │
       │          ┌──────────────┐           │
       └─────────▶│ MethodChannel│◀──────────┘
                  │  (Kotlin)    │
                  │  AudioFocus  │
                  │  MediaSession│
                  │  Notification│
                  └──────────────┘
```

### Threading Model

| Thread | Role | Priority |
|---|---|---|
| Dart Isolate | UI + API calls | Normal |
| AAudio Callback | Reads ring buffer, mixes 4 tracks, applies DSP, outputs to speaker | Real-time (SCHED_FIFO) |
| FLAC Decoder | Decodes FLAC → PCM → writes to ring buffer | Normal |
| WAV Decoder | Decodes WAV → PCM → writes to ring buffer | Normal |
| Media Decoder | Decodes MP3/AAC/OGG via AMediaCodec → writes to ring buffer | Normal |
| MediaStream Decoder | Streams from URL via AMediaExtractor → writes to ring buffer | Normal |
| Kotlin Main | Android audio focus, media session, notifications | Normal |

### Lock-Free Primitives

- **Ring buffer:** SPSC (Single-Producer, Single-Consumer) lock-free. 512K samples capacity (~5.9s stereo at 44.1kHz).
- **eventfd:** Linux kernel mechanism for signaling decoder threads to stop (no polling, no mutex).
- **Atomic fields:** `volume`, `pan`, `mute`, `solo`, `repeatCount` are `std::atomic<float/int>` — safe to read/write from any thread without locks.

### Supported Formats

| Format | Decoder | Metadata | Gapless |
|---|---|---|---|
| FLAC | libFLAC | Vorbis Comments + CUESHEET | Yes |
| WAV | Native PCM parser | None | Yes |
| MP3 | AMediaCodec | None | Yes |
| AAC | AMediaCodec | None | Yes |
| OGG Vorbis | AMediaCodec | None | Yes |
| M4A | AMediaCodec | None | Yes |
| URL Stream | AMediaExtractor | None | No (crossfade only) |
