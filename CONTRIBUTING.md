<div align="center">
  <br>
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/flutter/flutter.png" alt="Flutter" width="80" />
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/android/android.png" alt="Android" width="80" />
  <br>
  <br>
  <h1>Contributing to Arc Audio Engine (AAE)</h1>
  <p>
    <strong>Guidelines for contributing to the native multi-format audio playback plugin for Flutter on Android</strong>
  </p>
  <p>
    <a href="#-how-to-contribute">How to Contribute</a> •
    <a href="#-development-environment">Development</a> •
    <a href="#-best-practices">Best Practices</a> •
    <a href="#-code-of-conduct">Code of Conduct</a>
  </p>
  <p>
    <a href="README.md">README (English)</a> •
    <a href="README.es.md">README (Spanish)</a>
  </p>
  <div align="center">
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" /></a>
    <img src="https://img.shields.io/badge/Flutter-3.22+-02569B?logo=flutter" alt="Flutter 3.22+" />
    <img src="https://img.shields.io/badge/Android-API_27+-3DDC84?logo=android" alt="Android API 27+" />
  </div>
  <br>
</div>

---

## 📖 How to Contribute

Thank you for your interest in contributing to **Arc Audio Engine (AAE)**! This project is a Flutter plugin that provides native multi-format audio playback for Android via FFI.

We welcome any kind of contribution, whether it's reporting a bug, suggesting an improvement, or submitting code.

---

## 🌟 Contribution Process

### 1. Reporting Issues

If you find a bug or have a suggestion:

1. **Check** if a similar issue already exists in the issue tracker.
2. **Open a new issue** using the appropriate template:
   - **Bug report** — if something doesn't work as expected
   - **Feature request** — if you have an idea to improve the plugin
3. **Be descriptive**: include reproduction steps, logs, Flutter/Android version, and any relevant details.

### 2. Submitting Code (Pull Requests)

#### Workflow

```bash
# 1. Fork the repository
# 2. Clone your fork
git clone https://github.com/FYSPA/Arc-Engine.git
cd Arc-Engine

# 3. Create a branch for your change
git checkout -b feature/short-descriptive-name

# 4. Make your changes...
# 5. Make sure everything works
dart format --output none --set-exit-if-changed .
flutter analyze
flutter test

# 6. (Optional) Validate the package before publishing
dart pub publish --dry-run

# 7. Commit with a clear message
git commit -m "feat: clear description of the change"

# 8. Push and open a Pull Request
git push origin feature/short-descriptive-name
```

#### Pull Request Guidelines

| Aspect | Requirement |
|--------|------------|
| **Code style** | Follow existing conventions in the project |
| **Commit messages** | Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`, etc.) |
| **Code formatting** | `dart format --output none --set-exit-if-changed .` must pass |
| **Static analysis** | `flutter analyze` must pass with no errors |
| **Tests** | `flutter test` + C++ tests must pass. Add tests for new functionality |
| **Documentation** | Update README if your change modifies the API. Add dartdoc to all new public API members |
| **Base branch** | Always target `main` |
| **Single purpose** | Each PR should solve one problem or add one feature |

---

## 🧪 Development Environment

### Requirements

- Flutter SDK `3.44.5`
- Android NDK (included with Android Studio)
- Android device or emulator with API 27+

### Native Compilation

The plugin compiles native C++ code via CMake and FFI. The precompiled `libFLAC.a` and `libogg.a` libraries are located in `android/src/main/cpp/libs/`.

To build from scratch:

```bash
cd example
flutter pub get
flutter run
```

### Project Structure

```
arc_engine/
├── lib/
│   ├── arc_engine.dart           # Library barrel file (re-exports)
│   └── src/
│       ├── ffi_bindings.dart       # All FFI lookupFunction definitions
│       ├── track_player.dart       # TrackPlayer with streams + Timer polling
│       ├── audio_mixer.dart        # AudioEngine singleton, 4 tracks, EQ
│       └── pcm_stream.dart         # Real-time PCM stream for visualization
├── android/src/main/cpp/
│   ├── CMakeLists.txt              # Native build configuration
│   ├── audio_engine.cpp            # AAudio callback + FFI exports
│   ├── dispatcher.cpp/.h           # Format dispatch (track_play)
│   ├── engine_state.cpp/.h         # Global state (gCtl) + stopEngine/resetCtl
│   ├── engine_threads.cpp/.h       # Decoder threads (WAV/FLAC/Media/Stream)
│   ├── aaudio_utils.cpp/.h         # AAudio stream creation/management
│   ├── ring_buffer.h               # Lock-free SPSC ring buffer
│   ├── wav_handler.cpp/.h          # Legacy WAV blocking playback
│   ├── flac_handler.cpp/.h         # Legacy FLAC playback + metadata
│   ├── media_handler.cpp/.h        # Legacy media blocking playback
│   ├── common.h                    # Shared macros and types
│   ├── dsp_processor.h             # 10-band EQ with biquad filters
│   └── libs/                       # Precompiled libraries (FLAC + Ogg)
├── test/
│   ├── arc_engine_test.dart      # 10 tests — AudioEngine singleton, compat
│   ├── track_player_test.dart      # 14 tests — TrackPlayer lifecycle
│   ├── pcm_stream_test.dart        # 6 tests — PcmStream emit/stop/dispose
│   ├── fake_ffi.dart               # Mock FfiInterface for Dart tests
│   └── cpp/
│       ├── ring_buffer_test.cpp    # 10 tests — RingBuffer push/pop/wrap
│       ├── dsp_processor_test.cpp  # 12 tests — EQ filters, bypass, bounds
│       ├── ring_buffer_benchmark.cpp
│       ├── dsp_processor_benchmark.cpp
│       ├── build_and_run.sh
│       └── common.h                # Mock for host-based C++ testing
├── example/                        # Flutter example app
│   └── lib/
│       ├── main.dart
│       └── widgets/
│           ├── home_screen.dart
│           ├── audio_controls.dart
│           ├── library_status_card.dart
│           ├── pcm_visualizer.dart
│           ├── eq_dialog.dart
│           ├── status_display.dart
│           └── library_status_card.dart
├── LICENSE
├── CHANGELOG.md
├── CONTRIBUTING.md
├── ROADMAP.md
└── README.md
```

---

## 🧠 Best Practices

### Native C++ Code

- Use `__android_log_print` for logging (macros `LOGI`/`LOGE` already defined in `common.h`)
- Always free resources: `FLAC__stream_decoder_delete()`, `AAudioStream_close()`, `close(gCtl.stopFd)`
- Maintain backward compatibility of the FFI API
- The global state `gCtl` (EngineState) is in `engine_state.h` — modify with care
- Cross-thread signaling uses **eventfd** (kernel-level) to avoid memory visibility issues on ARM devices
- The SPSC ring buffer in `ring_buffer.h` is lock-free — do not add locks
- The decoder thread writes to the ring buffer; the AAudio callback reads — do not reverse these roles

### Dart Code

- Use `dart:ffi` with correct types (`Int32`, `Int64`, `Pointer<Utf8>`)
- Always free native memory with `calloc.free()` in `finally` blocks
- Handle exceptions with descriptive messages
- **Document all public API** with dartdoc comments (classes, methods, properties, enums)

### Testing

- **Dart tests** (`flutter test`): 30+ unit tests covering TrackPlayer, AudioEngine, and PcmStream
- **C++ tests** (`bash test/cpp/build_and_run.sh`): 22+ unit tests for RingBuffer and DspProcessor, plus latency benchmarks
- Add tests when modifying existing functionality
- For new features requiring FFI interaction, extend `test/fake_ffi.dart` with the new mock methods

### Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add WAV playback support
fix: fix memory leak in get_flac_info
docs: update README with usage examples
refactor: simplify metadata callback
test: add tests for FlacInfo struct
```

---

## 📜 Code of Conduct

This project follows the [Contributor Covenant Code of Conduct](https://www.contributor-covenant.org/). By participating, you are expected to maintain a respectful and inclusive environment.

---

## ❓ Questions?

If you have questions about contributing, feel free to:

- Open an issue with the `question` label
- Contact the maintainer: **FYSPA**

Thank you for making Arc Audio Engine (AAE) a better project!

---

<div align="center">
  <br>
  <p>
    Made with <strong>Flutter</strong> + <strong>C++</strong> + <strong>FLAC</strong>
  </p>
  <p>
    <a href="README.md">README (English)</a> •
    <a href="README.es.md">README (Spanish)</a>
  </p>
  <br>
</div>
