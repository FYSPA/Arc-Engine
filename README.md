<div align="center">
  <br>
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/flutter/flutter.png" alt="Flutter" width="80" />
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/android/android.png" alt="Android" width="80" />
  <br>
  <br>
  <h1>ARC Audio Engine</h1>
  <p>
    <strong>Native multi-format audio playback for Flutter on Android</strong>
  </p>
  <p>
    <a href="#-about-the-project">About</a> •
    <a href="#-features">Features</a> •
    <a href="#-how-it-works">How It Works</a> •
    <a href="#-getting-started">Getting Started</a> •
    <a href="#-project-structure">Structure</a> •
    <a href="#-usage">Usage</a> •
    <a href="#-tech-stack">Tech Stack</a>
  </p>
  <div align="center">
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" /></a>
    <img src="https://img.shields.io/badge/Flutter-3.4+-02569B?logo=flutter" alt="Flutter 3.4+" />
    <img src="https://img.shields.io/badge/Android-API_27+-3DDC84?logo=android" alt="Android API 27+" />
  </div>
  <br>
</div>

---

## 📖 About the Project

A high-performance Flutter plugin that brings **native multi-format audio playback** to Android using **FFI** (Foreign Function Interface).

Audio Engine supports **FLAC, WAV, MP3, AAC, and OGG** formats with low-latency AAudio callback output. This plugin allows Flutter developers to:

- **Play** FLAC, WAV, MP3, AAC, and OGG files natively
- **Pause / Resume / Seek / Stop** controls with slider UI
- Low-latency AAudio **callback mode** with lock-free **SPSC ring buffer**
- **eventfd** kernel-based cross-thread signaling for reliable controls
- Pure C++ native code — no Java/Kotlin bridge overhead

> **Note:** Uses `libFLAC` (decoding), `AAudio` (low-latency audio), `AMediaCodec` (MP3/AAC/OGG), and custom WAV parser — all via **dart:ffi**!

---

## ✨ Features

<table>
  <tr>
    <td><strong>Multi-format</strong></td>
    <td>FLAC (libFLAC), WAV (native parser), MP3/AAC/OGG (AMediaCodec)</td>
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
    <td><strong>FFI Bridge</strong></td>
    <td>Direct C++ to Dart communication — no platform channels overhead</td>
  </tr>
  <tr>
    <td><strong>Android Only</strong></td>
    <td>Optimized for Android with AAudio and NDK (API 27+)</td>
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
│  │         AudioEngine (audio_engine.dart)                │   │
│  │  startAudio() │ stop() │ pause() │ resume() │ seek()   │   │
│  └───────────────┴────────┴─────────┴──────────┴────────┘   │
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
│  │          │         ▼ stop sig │  │mediaPlayback   │  │   │
│  └──────────┘  ┌──────────┐     │  │Thread          │  │   │
│                │aaudio_   │     │  └────────┬───────┘  │   │
│                │utils     │     │           │          │   │
│                │(create,  │     │           ▼          │   │
│                │ close)   │     │  ┌────────────────┐  │   │
│                └──────────┘     │  │  Ring Buffer   │  │   │
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

1. **Dart** calls `AudioEngine.startAudio(path)` via FFI
2. **dispatcher.cpp** parses the file extension and delegates to the appropriate format handler
3. **Parser thread** (WAV/FLAC/Media) decodes audio and pushes float PCM samples into the **lock-free SPSC ring buffer**
4. **AAudio callback** (`aaudioDataCallback`) runs in a high-priority audio thread, pops samples from the ring buffer, and writes them to the audio device
5. **Controls** (stop/pause/seek) use **eventfd** kernel signaling for reliable cross-thread communication

---

## 🚀 Getting Started

### Prerequisites

- Flutter SDK `>=3.4.0` (compatible with Dart `>=3.4.0`)
- Android device or emulator running **API 27+** (AAudio requirement)
- A **FLAC audio file** on your device

### Installation

1. **Clone the repository** and navigate to the example:
   ```bash
   cd audio_engine/example
   ```

2. **Install dependencies:**
   ```bash
   flutter pub get
   ```

3. **Build & run:**
   ```bash
   flutter run
   ```

4. **Push a test FLAC file** to your device (or use a file manager):
   ```bash
   adb push /path/to/test.flac /storage/emulated/0/Android/data/com.example.audio_engine_example/files/
   ```
   The example app looks for a file named `test.flac` at that location.

---

## 📁 Project Structure

```
example/
├── android/                          # Android platform files (Flutter)
├── lib/
│   └── main.dart                     # Example app UI
├── test/
│   └── widget_test.dart              # Widget tests
├── pubspec.yaml                      # Package configuration
└── README.md                         # You are here
```

### Key Files in the Plugin

```
audio_engine/                          # Root plugin package
├── lib/
│   └── audio_engine.dart              # Dart FFI bindings
├── android/src/main/cpp/
│   ├── CMakeLists.txt                 # Native build config
│   ├── audio_engine.cpp               # AAudio callback + FFI exports
│   ├── dispatcher.cpp/.h              # Format dispatch (start_audio, play_audio)
│   ├── engine_state.cpp/.h            # Global state (gCtl) + stopEngine/resetCtl
│   ├── engine_threads.cpp/.h          # Decoder threads (WAV/FLAC/Media)
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
└── pubspec.yaml                       # Plugin manifest
```

---

## 🎮 Usage

### Starting Playback (non-blocking engine)

```dart
import 'package:audio_engine/audio_engine.dart';

// Start playback (returns immediately, plays in background thread)
int result = AudioEngine.startAudio('/path/to/file.wav');
if (result == 0) print('Playback started!');
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
| `-2` | Invalid WAV RIFF header             |
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
      <td>Media Codecs</td>
      <td><strong>AMediaCodec</strong> (NDK)</td>
      <td>MP3, AAC, OGG playback</td>
    </tr>
  </table>
</div>

---

## 📸 Example App Preview

The example app provides a simple UI with:

- **Format buttons** — play FLAC, WAV, MP3, AAC, or OGG test files
- **Seek slider** — drag to seek within the audio
- **Pause / Resume / Stop** controls
- **Status display** — shows current playback state

> Test files are expected at:  
> `/storage/emulated/0/Android/data/com.example.audio_engine_example/files/`

---

## ❓ FAQ

### Why use eventfd instead of std::atomic for signaling?

On certain Android devices (e.g., Moto E6 Play with Snapdragon 427), the `std::thread` constructor's memory barrier fails to make the creating thread's writes visible to the new thread. Neither `std::atomic`, `volatile`, nor `std::atomic_thread_fence` worked reliably across threads. eventfd is a kernel-level syscall that guarantees proper memory ordering, making cross-thread signaling reliable regardless of platform memory model bugs.

### Why AAudio callback instead of blocking writes?

Callback mode runs in a high-priority audio thread, reducing latency and preventing underruns. The lock-free SPSC ring buffer decouples the decoder thread from the audio thread, allowing smooth playback even when decoding takes variable time. Blocking `AAudioStream_write()` would stall if the decoder cannot keep up.

### What is the ring buffer?

A lock-free Single-Producer Single-Consumer (SPSC) ring buffer with 65536 sample capacity. The decoder thread writes PCM float samples to the buffer, and the AAudio callback reads them. No mutexes or atomic operations are needed in the hot path, only careful memory ordering using acquire/release semantics.

### What formats are supported?

FLAC (via libFLAC), WAV (native parser), MP3, AAC, and OGG (via AMediaCodec). WAV uses a custom zero-copy parser, FLAC uses the reference libFLAC decoder, and compressed formats use Android's AMediaCodec NDK API.

### What Android API level is required?

API 27+ is required for AAudio. The plugin uses AAudio's callback mode which was stabilized in Android 8.1 (API 27).

### How do I test playback?

Push a test file to the device and tap the corresponding format button in the example app:

```bash
adb push test.flac /storage/emulated/0/Android/data/com.example.audio_engine_example/files/
```

---

## 📄 License

Distributed under the MIT License.

---

<div align="center">
  <br>
  <p>
    Made with <strong>Flutter</strong> + <strong>C++</strong> + <strong>FLAC</strong>
  </p>
  <p>
    <a href="https://github.com/your-org/audio_engine">Back to Plugin</a>
  </p>
  <br>
</div>
