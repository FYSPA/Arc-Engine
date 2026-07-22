<div align="center">
  <br>
  <img src="assets/Logo.png" alt="Arc Engine Logo" width="300" />
  <h1>Arc Audio Engine (AAE)</h1>
  <p>
    <strong>Native multi-format audio playback for Flutter on Android</strong>
  </p>
  <p>
    <a href="#-about-the-project">About</a> •
    <a href="#-features">Features</a> •
    <a href="#-how-it-works">How It Works</a> •
    <a href="#-getting-started">Getting Started</a> •
    <a href="#-usage">Usage</a> •
    <a href="#-tech-stack">Tech Stack</a>
  </p>
   <p>
    <a href="README.es.md">Leer en Español</a> •
    <a href="./CONTRIBUTING.md">Contributing</a>
  </p>
  <div align="center">
    <a href="https://pub.dev/packages/arc_engine"><img src="https://img.shields.io/pub/v/arc_engine" alt="pub.dev" /></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" /></a>
    <img src="https://img.shields.io/badge/Flutter-3.22+-02569B?logo=flutter" alt="Flutter 3.22+" />
    <img src="https://img.shields.io/badge/Android-API_27+-3DDC84?logo=android" alt="Android API 27+" />
  </div>
  <br>
</div>

---

## 📖 About the Project

A high-performance Flutter plugin that brings **native multi-format audio playback** to Android using **FFI** (Foreign Function Interface).

Arc Audio Engine (AAE) supports **FLAC, WAV, MP3, AAC, and OGG** formats with low-latency AAudio callback output. It also supports **URL streaming** via Android MediaExtractor (API 29+) with automatic fallback to download-then-play on older devices.

- **Play** FLAC, WAV, MP3, AAC, and OGG files natively
- **Stream** audio from HTTP URLs
- **Pause / Resume / Seek / Stop** controls with slider UI
- Low-latency AAudio **callback mode** with lock-free **SPSC ring buffer**
- **eventfd** kernel-based cross-thread signaling for reliable controls
- Pure C++ native code — no Java/Kotlin bridge overhead

> **Note:** Uses `libFLAC` (decoding), `AAudio` (low-latency audio), `AMediaCodec` / `AMediaExtractor` (MP3/AAC/OGG streaming), and custom WAV parser — all via **dart:ffi**!

---

## ✨ Features

<table>
  <tr>
    <td><strong>Multi-format Playback</strong></td>
    <td>FLAC (libFLAC), WAV (native parser), MP3/AAC/OGG (AMediaCodec)</td>
  </tr>
  <tr>
    <td><strong>URL Streaming</strong></td>
    <td>Native HTTP streaming (API 29+) with download-then-play fallback for older devices. Configurable URL dialog with progress bar and cancel.</td>
  </tr>
  <tr>
    <td><strong>Native Playback</strong></td>
    <td>AAudio callback mode with PCM float output and lock-free ring buffer</td>
  </tr>
  <tr>
    <td><strong>Controls</strong></td>
    <td>Pause / Resume / Seek / Stop with eventfd kernel signaling</td>
  </tr>
  <tr>
    <td><strong>Multi-track Mixer</strong></td>
    <td>Up to 4 concurrent tracks with per-track volume, pan, and independent transport controls</td>
  </tr>
  <tr>
    <td><strong>10-band DSP EQ</strong></td>
    <td>Global equalizer with peaking, low/high-shelf, and low/high-pass filter types</td>
  </tr>
  <tr>
    <td><strong>PCM Stream to Dart</strong></td>
    <td>Real-time raw PCM samples stream for visualization (VU meter, waveform)</td>
  </tr>
  <tr>
    <td><strong>File Picker</strong></td>
    <td>Import audio files via SAF file picker (FLAC, WAV, MP3, AAC, OGG, M4A)</td>
  </tr>
  <tr>
    <td><strong>FFI Bridge</strong></td>
    <td>Direct C++ to Dart communication — no platform channels overhead</td>
  </tr>
  <tr>
    <td><strong>Low Latency</strong></td>
    <td>SPSC ring buffer + AAudio callback for minimal audio latency</td>
  </tr>
</table>

---

## ⚙️ How It Works

```
┌──────────────────────────────────────────────────────────────┐
│                     Flutter (Dart)                            │
│  ┌───────────────────────────────────────────────────────┐   │
│  │         AudioEngine (arc_engine.dart)                │   │
│  │  startAudio() │ streamUrl() │ stop() │ pause()        │   │
│  │  resume() │ seek() │ startPcmStream()                  │   │
│  └───────────────┴────────────┴─────────┴────────────────┘   │
│                          │ dart:ffi                          │
└──────────────────────────┼──────────────────────────────────┘
                           ▼
┌──────────────────────────────────────────────────────────────┐
│              Native C++ — libaudio_engine.so                  │
│                                                              │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │dispatcher│─▶│engine_state   │  │  Decoder Threads     │   │
│  │.cpp      │  │(gCtl, stop,  │  │  ┌────────────────┐  │   │
│  │          │  │ reset)       │  │  │wavPlaybackThread│  │   │
│  │start_    │  └──────┬───────┘  │  │flacPlayback    │  │   │
│  │audio()   │         │ eventfd  │  │Thread          │  │   │
│  │start_    │         ▼ stop sig │  │mediaPlayback   │  │   │
│  │media_    │  ┌──────────┐     │  │Thread          │  │   │
│  │stream()  │  │aaudio_   │     │  │mediaStream     │  │   │
│  └──────────┘  │utils     │     │  │PlaybackThread  │  │   │
│                │(create,  │     │  └────────┬───────┘  │   │
│                │ close)   │     │           │          │   │
│                └──────────┘     │           ▼          │   │
│                                 │  ┌────────────────┐  │   │
│                                 │  │  Ring Buffer   │  │   │
│                                 │  │ (SPSC lock-free)│  │   │
│                                 │  └────────┬───────┘  │   │
│                                 └───────────┼──────────┘   │
│                                             │              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  │              │
│  │wav_      │  │flac_     │  │media_    │  │              │
│  │handler   │  │handler   │  │handler   │  │              │
│  │(legacy)  │  │(legacy)  │  │(legacy)  │  │              │
│  └──────────┘  └──────────┘  └──────────┘  │              │
│                                             ▼              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │            AAudio Callback (audio_engine.cpp)         │  │
│  │  aaudioDataCallback() — reads from ring buffer        │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
                  Android Audio HAL
```

### Data Flow

1. **Dart** calls `AudioEngine.startAudio(path)` or `AudioEngine.streamUrl(url)` via FFI
2. **dispatcher.cpp** routes to the appropriate handler (local file by extension, or URL stream via MediaExtractor)
3. **Decoder thread** (WAV/FLAC/Media/Stream) decodes audio and pushes float PCM samples into the **lock-free SPSC ring buffer**
4. **AAudio callback** (`aaudioDataCallback`) runs in a high-priority audio thread, pops samples from the ring buffer, and writes them to the audio device
5. **Controls** (stop/pause/seek) use **eventfd** kernel signaling for reliable cross-thread communication

---

## 🚀 Getting Started

### Prerequisites

- Flutter SDK `>=3.22.0` (compatible with Dart `>=3.4.0`)
- Android device or emulator running **API 27+** (AAudio requirement)

### Installation

Add to your `pubspec.yaml`:

```yaml
dependencies:
  arc_engine: ^0.1.0
```

Or install via command line:

```bash
dart pub add arc_engine
flutter pub get
```

> The plugin only supports Android (API 27+). It uses AAudio's callback mode which was stabilized in Android 8.1.

### Try the Example App

```bash
cd example
flutter pub get
flutter run
```

Add audio files using the **Pick Files** button in the app, or push files via adb:

```bash
adb push test.flac /storage/emulated/0/Android/data/com.example.arc_engine_example/files/
```

> The example app uses `path_provider` for internal storage and `file_picker` (SAF) for importing audio files, bypassing Android 11+ scoped storage restrictions.

---

## 📁 Project Structure

```
arc_engine/                          # Root plugin package
├── lib/
│   └── arc_engine.dart              # Library barrel file (public API)
├── android/src/main/cpp/
│   ├── CMakeLists.txt                 # Native build config
│   ├── audio_engine.cpp               # AAudio callback + FFI exports
│   ├── dispatcher.cpp/.h              # Format dispatch (start_audio, start_media_stream)
│   ├── engine_state.cpp/.h            # Global state (gCtl) + stopEngine/resetCtl
│   ├── engine_threads.cpp/.h          # Decoder threads (WAV/FLAC/Media/Stream)
│   ├── aaudio_utils.cpp/.h            # AAudio stream creation/management
│   ├── ring_buffer.h                  # Lock-free SPSC ring buffer
│   ├── wav_handler.cpp/.h             # Legacy WAV blocking playback
│   ├── flac_handler.cpp/.h            # Legacy FLAC blocking playback
│   ├── media_handler.cpp/.h           # Legacy Media blocking playback
│   ├── common.h                       # Shared macros and types
│   └── libs/                          # Precompiled static libraries
│       ├── include/                   #   FLAC/Ogg headers
│       ├── arm64-v8a/                 #   libFLAC.a + libogg.a (64-bit)
│       └── armeabi-v7a/               #   libFLAC.a + libogg.a (32-bit)
├── example/                           # Flutter example app
│   └── lib/
│       ├── main.dart                  # App entry point
│       └── widgets/
│           ├── home_screen.dart       # Main UI (file list, controls, streaming)
│           ├── audio_controls.dart    # Legacy format buttons (unused)
│           ├── library_status_card.dart
│           ├── pcm_visualizer.dart    # VU meter + waveform
│           └── status_display.dart    # Animated status messages
├── LICENSE
├── CHANGELOG.md
├── CONTRIBUTING.md
└── README.md
```

---

## 🎮 Usage

### Starting Local Playback (non-blocking engine)

```dart
import 'package:arc_engine/arc_engine.dart';

// Start playback (returns immediately, plays in background thread)
int result = AudioEngine.startAudio('/path/to/file.wav');
if (result == 0) print('Playback started!');
```

### URL Streaming

```dart
// Try native streaming (returns 0 on success, negative on error)
int result = AudioEngine.streamUrl('https://example.com/audio.mp3');
if (result == 0) {
  print('Streaming...');
} else {
  print('Streaming unavailable, use download fallback');
}
```

> Native streaming requires Android API 29+. On older devices, `streamUrl()` returns an error code and you should fall back to download-then-play.

### PCM Stream for Visualization

```dart
// Get a broadcast stream of raw PCM float samples
Stream<List<double>> pcmStream = AudioEngine.startPcmStream(
  interval: Duration(milliseconds: 50),  // update interval
);

pcmStream.listen((samples) {
  // samples is a List<double> of interleaved float PCM (-1.0 to 1.0)
  updateVisualizer(samples);
});
```

### Controls

```dart
AudioEngine.stop();     // Stop playback immediately
AudioEngine.pause();    // Pause playback
AudioEngine.resume();   // Resume from pause
AudioEngine.seek(ms);   // Seek to position in milliseconds
```

### Status Queries

```dart
bool playing = AudioEngine.isPlaying;  // Is engine active?
int pos = AudioEngine.getPosition();    // Current position in ms
int dur = AudioEngine.getDuration();    // Total duration in ms
```

### Error Codes

| Code | Description                         |
|:----:|-------------------------------------|
| `0`  | Success                             |
| `-1` | File not found / no extension       |
| `-2` | Invalid WAV RIFF header / Streaming not supported on this device |
| `-3` | Invalid WAV fmt chunk               |
| `-4` | Unsupported WAV format              |
| `-5` | WAV no fmt chunk                    |
| `-6` | WAV data read error                 |
| `-7` | WAV no data chunk                   |
| `-8` | Failed to create eventfd (signaling)|

---

## 🛠️ Tech Stack

<div align="center">
  <table>
    <tr>
      <th>Layer</th>
      <th>Technology</th>
      <th>Purpose</th>
    </tr>
    <tr>
      <td>UI</td>
      <td><strong>Flutter</strong></td>
      <td>Cross-platform UI framework</td>
    </tr>
    <tr>
      <td>Bridge</td>
      <td><strong>dart:ffi</strong></td>
      <td>Direct native code invocation</td>
    </tr>
    <tr>
      <td>Decoding</td>
      <td><strong>libFLAC</strong> (<a href="https://xiph.org/flac/">Xiph.Org</a>)</td>
      <td>FLAC audio codec library</td>
    </tr>
    <tr>
      <td>Bitstream</td>
      <td><strong>libogg</strong></td>
      <td>Ogg container (FLAC dependency)</td>
    </tr>
    <tr>
      <td>Audio Output</td>
      <td><strong>AAudio</strong> (Android NDK)</td>
      <td>Low-latency callback mode native audio</td>
    </tr>
    <tr>
      <td>Ring Buffer</td>
      <td><strong>Custom SPSC</strong></td>
      <td>Lock-free single-producer single-consumer</td>
    </tr>
    <tr>
      <td>Signaling</td>
      <td><strong>eventfd</strong></td>
      <td>Kernel-based cross-thread stop signaling</td>
    </tr>
    <tr>
      <td>DSP Processor</td>
      <td><strong>Custom C++</strong></td>
      <td>10-band biquad EQ with peaking, shelf, and pass filter types</td>
    </tr>
    <tr>
      <td>Media Codecs</td>
      <td><strong>AMediaCodec</strong> (NDK)</td>
      <td>MP3, AAC, OGG playback</td>
    </tr>
    <tr>
      <td>URL Streaming</td>
      <td><strong>AMediaExtractor</strong> (NDK)</td>
      <td>HTTP audio streaming (API 29+)</td>
    </tr>
    <tr>
    <td>File Picker</td>
    <td><strong>file_picker</strong></td>
    <td>SAF-based audio file import</td>
    </tr>
    <tr>
      <td>Preferences</td>
      <td><strong>shared_preferences</strong></td>
      <td>Persist last stream URL</td>
    </tr>
    <tr>
      <td>App Storage</td>
      <td><strong>path_provider</strong></td>
      <td>Internal documents directory</td>
    </tr>
  </table>
</div>

---

## 📸 Example App Preview

The example app provides a full-featured UI with:

- **File picker** — import audio files via SAF system picker (copies to app directory)
- **Stream from URL** — dialog to enter any direct audio URL (with persist and history)
- **Download progress** — progress bar with cancel button during stream fallback
- **Local file playback** — tap any file to play (FLAC, WAV, MP3, AAC, OGG, M4A)
- **Seek slider** — drag to seek within the audio
- **Pause / Resume / Stop** controls
- **PCM visualizer** — real-time VU meter and waveform from decoded samples
- **Status display** — animated status messages
- **File picker** — Pick Files button to import audio via SAF

> The app stores its audio files in the directory returned by `getApplicationDocumentsDirectory()` (`path_provider`).

---

## 📚 API Reference

### `AudioEngine` — Central orchestrator

| Member | Description |
|--------|-------------|
| `AudioEngine.instance` | Singleton accessor |
| `masterVolume` | Get/set master output volume (0.0–1.0) |
| `tracks` | Unmodifiable list of 4 [`TrackPlayer`]s |
| `startPcmStream(interval:)` | Start PCM visualization stream |
| `stopPcmStream()` | Stop PCM stream |
| `startAudio(path)` | *(legacy)* Start local playback on track 0 |
| `streamUrl(url)` | *(legacy)* Stream from URL on track 0 |
| `stop()` / `pause()` / `resume()` / `seek(ms)` | *(legacy)* Transport controls on track 0 |
| `setEqBand(i, type, freq, gain, q)` | Configure EQ band (global) |
| `setEqBypass(bool)` / `resetEq()` | EQ global controls |

### `TrackPlayer` — Per-track control

| Member | Description |
|--------|-------------|
| `play(path)` | Load and start playback |
| `stop()` / `pause()` / `resume()` | Transport controls |
| `seek(Duration)` | Seek to position |
| `volume` / `pan` | Per-track volume (0–1) / pan (-1–1) |
| `state` | Current [`PlaybackState`] |
| `position` / `duration` | Current position / total duration |
| `onStateChanged` | Stream of [`PlaybackState`] changes |
| `onPositionChanged` | Stream of position [`Duration`] updates |
| `dispose()` | Release resources |

### `PlaybackState` enum

`stopped` — no active playback  
`playing` — actively decoding and outputting  
`paused` — suspended, position preserved

### `PcmStream` — Real-time PCM

`start({interval})` → returns a broadcast `Stream<List<double>>` of interleaved float samples (-1.0 to 1.0).  
`stop()` / `dispose()` — stops the stream.

### `FlacInfo` — FLAC metadata struct

Fields: `sampleRate`, `channels`, `bitsPerSample`, `totalSamples`, `durationMs`.

> Full dartdoc is available on [pub.dev](https://pub.dev/documentation/arc_engine/latest/).

---

## ❓ FAQ

### Why use eventfd instead of std::atomic for signaling?

On certain Android devices (e.g., Moto E6 Play with MediaTek MT6739), the `std::thread` constructor's memory barrier fails to make the creating thread's writes visible to the new thread. Neither `std::atomic`, `volatile`, nor `std::atomic_thread_fence` worked reliably across threads. eventfd is a kernel-level syscall that guarantees proper memory ordering, making cross-thread signaling reliable regardless of platform memory model bugs.

### Why AAudio callback instead of blocking writes?

Callback mode runs in a high-priority audio thread, reducing latency and preventing underruns. The lock-free SPSC ring buffer decouples the decoder thread from the audio thread, allowing smooth playback even when decoding takes variable time. Blocking `AAudioStream_write()` would stall if the decoder cannot keep up.

### What is the ring buffer?

A lock-free Single-Producer Single-Consumer (SPSC) ring buffer with 65536 sample capacity. The decoder thread writes PCM float samples to the buffer, and the AAudio callback reads them. No mutexes or atomic operations are needed in the hot path, only careful memory ordering using acquire/release semantics.

### What formats are supported?

FLAC (via libFLAC), WAV (native parser), MP3, AAC, and OGG (via AMediaCodec). WAV uses a custom zero-copy parser, FLAC uses the reference libFLAC decoder, and compressed formats use Android's AMediaCodec NDK API.

### Does URL streaming work on all devices?

Native URL streaming via `AMediaExtractor_setDataSource()` requires **Android API 29+**. On older devices or devices where native streaming fails, the example app falls back to downloading the file and playing it locally. You can also implement your own download-then-play flow by detecting the error code.

### What Android API level is required?

API 27+ is required for AAudio. The plugin uses AAudio's callback mode which was stabilized in Android 8.1 (API 27).

### Does FLAC's LGPL license affect my app?

Arc Audio Engine statically links `libFLAC` (Xiph.Org), which is licensed under the **LGPL**. When you use this plugin in your Flutter app:

- You must comply with the LGPL terms for the FLAC library
- Since the plugin links FLAC **statically** into `libaudio_engine.so`, your app must allow users to **replace** the FLAC library with a modified version
- In practice, this means you should include a notice (e.g. in your app's "About" or "Licenses" screen) crediting Xiph.Org and stating that the FLAC library is available under LGPL
- The simplest way to comply is to use Flutter's built-in [`LicenseRegistry`](https://api.flutter.dev/flutter/foundation/LicenseRegistry-class.html) or add an open-source notice page

### How do I add audio files from my device?

Use the **Pick Files** button (bottom card) to open the system file picker. On Android 11+, this uses the Storage Access Framework (SAF) which bypasses scoped storage restrictions. Selected files are copied to the app's internal directory for playback. You can select multiple audio files at once.

---

## 📄 License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.

---

<div align="center">
  <br>
  <p>
    Made with <strong>Flutter</strong> + <strong>C++</strong> + <strong>FLAC</strong>
  </p>
  <p>
    <a href="./CONTRIBUTING.md">Contributing</a>
  </p>
  <br>
</div>
