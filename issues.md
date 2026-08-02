# Arc Audio Engine (AAE) — Issues Report

Generated from a full codebase review on 2026-07-21.

---

## Table of Contents

- [Critical Issues](#critical-issues)
- [Moderate Issues](#moderate-issues)
- [Build / Config Issues](#build--config-issues)
- [Long-Term / Architectural Concerns](#long-term--architectural-concerns)
- [What's Good](#whats-good)
- [Recommended Priority Order](#recommended-priority-order)

---

## Critical Issues

### 1. ~~`volatile` used instead of `std::atomic` (Data Races)~~ [FIXED]

- **File:** `android/src/main/cpp/engine_state.h:43-56`
- **Severity:** High
- **Description:** Seven fields in `TrackState` use `volatile int` for cross-thread communication: `running`, `paused`, `mute`, `solo`, `loop`, `hasNext`, `gapLessVersion`. In C++, `volatile` does **not** provide atomicity or memory ordering guarantees. When one thread writes and another reads simultaneously, this is undefined behavior.
- **Note:** `writtenFrames` and `seekToFrame` correctly use `std::atomic<int64_t>`, so the pattern exists but was not applied consistently.
- **Fix:** Replace all `volatile int` fields with `std::atomic<int>`.
- **Status:** Fixed in commit `758fcf1` — all `volatile int` fields replaced with `std::atomic<int>`.

### 2. ~~Mutex Lock Inside AAudio Callback Thread (Priority Inversion)~~ [FIXED]

- **File:** `android/src/main/cpp/effect.h:59`
- **Severity:** High
- **Description:** `EffectChain::process()` takes a `std::lock_guard<std::mutex>` on every callback invocation. This runs in the AAudio high-priority audio thread. If the UI/Dart thread holds the lock (via `fx_add`, `fx_remove`, or `fx_clear`), the audio thread blocks — causing audio glitches or buffer underruns.
- **Additional risk:** `EffectChain::find()` (used by compressor/reverb parameter setters) also locks the mutex, but the returned pointer is used **after** the lock is released, which is a use-after-free if the effect is removed concurrently.
- **Fix:** Use a lock-free technique: double-buffered effect list, atomic pointer swap, or `std::atomic<std::shared_ptr>`.
- **Status:** Fixed in commit `2bab6db` — lock-free EffectChain with double-buffered snapshot eliminates mutex in audio callback.

### 3. ~~Heap Allocation in AAudio Callback~~ [FIXED]

- **File:** `android/src/main/cpp/reverb.cpp:144`
- **Severity:** High
- **Description:** `Reverb::process()` allocates and frees a heap buffer on every callback (every ~4ms at 48kHz):
  ```cpp
  float *mono = new float[numFrames];
  ```
  This violates real-time audio constraints and can cause priority inversion or memory fragmentation.
- **Fix:** Use stack allocation (`float mono[numFrames]` with a safe upper bound) or a fixed-size pre-allocated buffer.
- **Status:** Fixed in commit `ad335cb` — replaced heap allocation with stack allocation.

### 4. ~~No AAudio Stream Disconnect Recovery~~ [FIXED]

- **File:** `android/src/main/cpp/aaudio_utils.cpp`
- **Severity:** High
- **Description:** Android can disconnect AAudio streams (e.g., when Bluetooth headphones connect/disconnect). The current code does not register an `AAudioStream_errorCallback` to handle `AAUDIO_ERROR_DISCONNECTED`. After a disconnection, playback silently fails with no recovery.
- **Fix:** Register an error callback that detects disconnection and triggers a stream restart or notifies Dart.
- **Status:** Fixed in commit `8a99106` — AAudio stream reconnection on disconnect with error callback.

### 5. ~~`cleanupEngine()` Leaks `fxChain`~~ [FIXED]

- **File:** `android/src/main/cpp/engine_state.cpp:63-76`
- **Severity:** High
- **Description:** `cleanupEngine()` closes the AAudio stream and deletes `dsp` and `limiter`, but does **not** delete `fxChain`. Meanwhile, `stop_audio()` and `track_stop()` in `audio_engine.cpp` **do** delete `fxChain`. This means calling `stopEngine()` leaks the effect chain.
- **Fix:** Add `delete gCtl.fxChain; gCtl.fxChain = nullptr;` to `cleanupEngine()`, or refactor so all teardown goes through `cleanupEngine()`.
- **Status:** Fixed in commit `ad335cb` — `cleanupEngine()` now includes `fxChain` deletion and is used by all teardown paths.

### 6. Gapless Transitions Ignore Format Mismatch (Audio Corruption)

- **File:** `android/src/main/cpp/engine_threads.cpp` (all 4 gapless paths: WAV, FLAC, Media, MediaStream)
- **Severity:** High
- **Description:** During gapless transitions, the new track's `sampleRate` and `channels` are loaded into `TrackState` and local variables, but the shared AAudio stream configuration (`gCtl.sampleRate`, `gCtl.outChannels`) is never updated. If the new track has a different channel count or sample rate than the stream, the decoder pushes audio in the wrong format and the AAudio callback reads it with the wrong format. This causes "slow" playback (stereo→mono: half speed), "fast/garbled" playback (mono→stereo: double speed), or pitch-shifted audio (sample rate mismatch).
- **Status:** FIXED — pre-check approach: format validated BEFORE destroying old resources. All 6 crash paths eliminated. On mismatch: native sets `gapLessAbort=1` + `gapLessVersion++`, Dart's `onGaplessAborted` stream emits the failed track name. Auto-play-on-abort implemented: when gapless is aborted, the UI automatically re-plays the queued track fresh (new AAudio stream at correct format) and re-queues the next track in the list. Also guarded `predecodeFlac` to only run for stereo streams (`gCtl.outChannels >= 2`).
- **Note:** True format conversion (resampling, channel remix) during gapless is out of scope — requires a per-thread resampler and is tracked as a future enhancement.

---

## Moderate Issues

### 6. ~~`_nameCtrl` StreamController Never Closed (Stream Leak)~~ [FIXED]

- **File:** `lib/src/track_player.dart:24`
- **Severity:** Medium
- **Description:** `_nameCtrl` is created as a broadcast `StreamController<String>`, but `dispose()` (line 242) closes `_stateCtrl` and `_posCtrl` but does **not** close `_nameCtrl`. This is a stream leak.
- **Fix:** Add `_nameCtrl.close()` in `dispose()`.
- **Status:** Fixed in commit `d6ff212` — added `_nameCtrl.close()` in `dispose()`.

### 7. ~~250ms Polling for Position Reporting~~ [FIXED]

- **File:** `lib/src/track_player.dart:260`
- **Severity:** Medium
- **Description:** `_startPolling()` uses `Timer.periodic(Duration(milliseconds: 250))` to poll native position/state. This introduces up to 250ms latency in UI position updates. The file header itself acknowledges this should use a native push callback (`Dart_PostCObject`).
- **Fix:** Implement native-to-Dart push via `Dart_PostCObject` or `NativeCallable` for real-time position updates.
- **Status:** Fixed — native push via `trackRegisterCallback` + `pushPositionToDart()` from all decoder threads. `_startPolling()` removed. Remaining 250ms timer is only for gapless version detection (intentional, not position).

### 8. ~~Debug `print()` Left in Production Code~~ [ACCEPTED]

- **File:** `lib/src/track_player.dart:257`
- **Severity:** Medium
- **Description:**
  ```dart
  print('TP[$index]: gapLess $_lastGapLessVersion->$curVersion nextName="$_nextName"');
  ```
  Uses `// ignore: avoid_print` but is still a debug statement that should use a proper logging framework or be removed for release builds.
- **Fix:** Replace with a configurable logger that can be disabled in release mode.
- **Status:** Accepted — kept as intentional debug logging with `// ignore: avoid_print` annotation.

### 9. ~~Duplicated Stream Cleanup Logic~~ [FIXED]

- **File:** `android/src/main/cpp/audio_engine.cpp:96-104, 172-181`
- **Severity:** Medium
- **Description:** The pattern of "check if any track is running; if not, close the stream and delete DSP/limiter/fxChain" appears identically in `stop_audio()` and `track_stop()`. The existing `cleanupEngine()` function in `engine_state.cpp` already exists for this purpose but is only called from `stopEngine()`.
- **Fix:** Refactor both functions to use `cleanupEngine()` (after fixing issue #5 to include `fxChain`).
- **Status:** Fixed in commit `d6ff212` — both `stop_audio()` and `track_stop()` now use `cleanupEngine()`.

### 10. ~~Duplicated WAV Parsing in `track_play()`~~ [FIXED]

- **File:** `android/src/main/cpp/dispatcher.cpp:81-120`
- **Severity:** Medium
- **Description:** `track_play()` contains ~40 lines of WAV parsing logic that duplicates `loadWavIntoState()` in `wav_handler.cpp`. The extension-lowercase conversion logic is also duplicated from `play_audio()`.
- **Fix:** Call `loadWavIntoState()` directly instead of inlining the parsing.
- **Status:** Fixed in commit `d6ff212` — `track_play()` now uses `loadWavIntoState()` and `normalizeExtension()`.

### 11. No Sample Rate Conversion Between Tracks

- **File:** `android/src/main/cpp/audio_engine.cpp`
- **Severity:** Medium
- **Description:** If two tracks have different sample rates (e.g., 44100Hz and 48000Hz), the mixer sums them directly without resampling. The first track to start sets the AAudio stream's sample rate; all subsequent tracks are expected to match. There is no automatic SRC.
- **Fix:** Add sample rate conversion in the mixer or reject tracks with mismatched sample rates with a clear error. (Acknowledged as V2 feature in ROADMAP.)

### 12. ~~Variable-Length Arrays (VLAs) on Stack in Audio Paths~~ [FIXED]

- **File:** `android/src/main/cpp/engine_threads.cpp` (multiple locations), `audio_engine.cpp:36`
- **Severity:** Medium
- **Description:** Patterns like `float floatBuf[chunk * ch]` where `chunk` can be up to 4096 and `ch` up to 2, resulting in 32KB+ on the stack. VLAs are non-standard in C++ (GCC extension). Combined with 4 tracks, stack usage in the audio callback can be substantial.
- **Fix:** Use fixed-size buffers with a compile-time maximum, or heap-allocate once at stream open and reuse.
- **Status:** Fixed in commit `da0baa9` — replaced VLAs with `std::vector<float>` throughout decoder threads.

### 13. `PcmStream` Has No Backpressure

- **File:** `lib/src/pcm_stream.dart:55-60`
- **Severity:** Medium
- **Description:** `PcmStream` reads PCM samples on a timer but never applies backpressure. If the Dart consumer is slower than the timer interval, samples are silently dropped with no notification.
- **Fix:** Add a flow control mechanism (e.g., pending flag, bounded queue, or callback-based demand).

---

## Build / Config Issues

### 14. `ROADMAP.md` in `.gitignore` But Still Tracked

- **File:** `.gitignore`, `ROADMAP.md`
- **Severity:** Low
- **Description:** The `.gitignore` lists `ROADMAP.md`, but the file exists in the repository. This is an inconsistency — either remove the file from tracking or remove the `.gitignore` entry.

### 15. `.idea/` Directory Committed

- **File:** `.idea/` (workspace.xml, audio_engine.iml, vcs.xml, misc.xml, modules.xml)
- **Severity:** Low
- **Description:** IDE-specific files are listed in `.gitignore` but are present in the repository. These should not be version-controlled.
- **Fix:** `git rm -r --cached .idea/`

### 16. `pubspec.lock` Committed Despite `.gitignore` Entry

- **File:** `.gitignore`, `pubspec.lock`
- **Severity:** Low
- **Description:** The `.gitignore` lists `pubspec.lock`, but the file is tracked. For a Flutter **library** package, committing the lock file is actually the correct practice. The `.gitignore` entry is wrong.
- **Fix:** Remove `pubspec.lock` from `.gitignore`.

### 17. `build/` Directory Committed

- **File:** `build/`
- **Severity:** Low
- **Description:** The `build/` directory (containing test cache, unit test assets, native assets JSON) is present and committed. The `.gitignore` lists `build/` but it was committed anyway.
- **Fix:** `git rm -r --cached build/`

### 18. Precompiled Libraries Only for Two ABIs

- **Files:** `android/src/main/cpp/libs/arm64-v8a/`, `android/src/main/cpp/libs/armeabi-v7a/`
- **Severity:** Medium
- **Description:** No `x86` or `x86_64` static libraries are provided, meaning the project cannot run on x86 emulators. The `.cxx` build directories show `x86` and `x86_64` profiles were attempted, which would fail at link time.
- **Fix:** Either provide x86/x86_64 builds of libFLAC.a and libogg.a, or document that emulator builds are unsupported.

---

## Long-Term / Architectural Concerns

### 19. Global Mutable State (`gCtl`)

- **File:** `android/src/main/cpp/engine_state.h`
- **Severity:** Long-term
- **Description:** `EngineState gCtl` is a global mutable struct accessed by the audio callback, decoder threads, and Dart FFI threads simultaneously. While some fields are atomic, many are not (see issue #1). The global state pattern makes it difficult to test the native code in isolation or support multiple engine instances.
- **Suggestion:** Encapsulate state in a class with explicit thread-safety annotations and access patterns.

### 20. ~~God Functions in Decoder Threads~~ [PARTIALLY FIXED]

- **File:** `android/src/main/cpp/engine_threads.cpp`
- **Severity:** Long-term
- **Description:** `flacPlaybackThread()` (~350 lines), `mediaPlaybackThread()` (~250 lines), and others are enormous functions with deeply nested conditionals. Each contains duplicated gapless-transition logic repeated ~3 times per thread type (~12 near-identical blocks total).
- **Suggestion:** Extract gapless transition into a shared helper function. Break each thread into smaller composable functions (init, decode loop, gapless transition, cleanup).
- **Status:** Partially fixed in commits `5e30fad` + `5298215` — 8 helpers extracted (Tanda 1 + Tanda 2): `initTrackEq`, `findAudioTrack`, `processCodecOutput`, `createCodecFromExtractor`, `initFirstTrackStream`. Gapless logic blocks reduced but not fully unified into a single helper.

### 21. Hardcoded 4-Track Limit

- **File:** `android/src/main/cpp/engine_state.h` (`MAX_TRACKS=4`), `lib/src/audio_mixer.dart` (`List.generate(4, ...)`)
- **Severity:** Long-term
- **Description:** The 4-track limit is hardcoded in both C++ and Dart. Increasing the track count requires coordinated changes in both layers.
- **Suggestion:** Make configurable via an `init()` parameter with a reasonable maximum.

### 22. No Thread-Safety Documentation or Annotations

- **File:** All C++ source files
- **Severity:** Long-term
- **Description:** No documentation on which fields are safe to access from which threads. The codebase lacks thread-safety annotations (e.g., `// GUARDED_BY`, `// LOCKS_EXCLUDED`, `// REQUIRES`).
- **Suggestion:** Add Clang thread-safety annotations to critical shared state.

### 23. Only Two Channels Max in DSP/EQ

- **File:** `android/src/main/cpp/dsp_processor.cpp`
- **Severity:** Long-term
- **Description:** `DspProcessor::process()` caps processing at 2 channels. Multi-channel sources (5.1, 7.1) have unprocessed channels beyond stereo.
- **Suggestion:** Extend EQ to support arbitrary channel counts. (Acknowledged as V2 in ROADMAP.)

### 24. No CI/CD Configuration

- **File:** Project root
- **Severity:** Long-term
- **Description:** No CI configuration files (`.github/workflows/`, `.gitlab-ci.yml`, etc.). Tests require either an Android device (integration) or manual C++ compilation via `build_and_run.sh`.
- **Suggestion:** Add a basic CI pipeline with Dart analysis, Dart unit tests, and C++ compilation check.

### 25. Test Coverage Gaps

- **File:** `test/`
- **Severity:** Long-term
- **Description:**
  - No Dart tests for `AudioFocus` class
  - No Dart tests for static EQ/compressor/reverb/limiter configuration APIs on `AudioEngine`
  - No Dart tests for `setNextTrack()` / `clearNextTrack()` gapless functionality
  - C++ tests only cover `DspProcessor` and `RingBuffer`; no tests for decoder threads, dispatcher, or effect chain
  - `FakeFfi` mock does not simulate `loop`, `setNext`, or `clearNext` behavior
- **Suggestion:** Expand `FakeFfi` coverage and add tests for all public API surface.

### 26. `FfiInterface` Instance Setter Is Not Thread-Safe

- **File:** `lib/src/ffi_bindings.dart`
- **Severity:** Low
- **Description:** `FfiInterface._instanceForTest` is a static nullable field with no synchronization. Setting it from one test while another reads it could cause issues in parallel test execution.
- **Suggestion:** Use a zone-based approach or `Isolate`-local storage for test isolation.

### 27. `strncpy` Without Explicit Null-Termination

- **File:** `android/src/main/cpp/engine_threads.cpp` (8 occurrences)
- **Severity:** Low
- **Description:** Pattern: `strncpy(trk.path, trk.nextPath, sizeof(trk.path) - 1)`. While `TrackState::path` is zero-initialized (`char path[512]{0}`), after a gapless transition the previous path data may still be present. The code relies on the `sizeof(trk.path) - 1` limit but does not explicitly set `trk.path[511] = '\0'` after the copy.
- **Fix:** Add `trk.path[sizeof(trk.path) - 1] = '\0';` after each `strncpy`, or use `snprintf` instead.

### 28. ~~`a0_` Array Declared But Not Used at Runtime~~ [FIXED]

- **File:** `android/src/main/cpp/dsp_processor.h:~137`
- **Severity:** Low
- **Description:** `double a0_[MAX_EQ_BANDS]` is computed in `recalcCoeffs()` but never read after the normalization step (`b0 /= a0`). Wastes 80 bytes of memory.
- **Fix:** Remove the `a0_` array member.
- **Status:** Fixed — replaced member `a0_[MAX_EQ_BANDS]` with local variable `double a0` in `recalcCoeffs()`. Saves 80 bytes per DspProcessor instance.

### 29. No `onError` Stream — Decoder Errors Are Silent

- **File:** `android/src/main/cpp/engine_threads.cpp` (all 4 decoder threads), `lib/src/track_player.dart`
- **Severity:** Medium
- **Description:** When a decoder encounters a mid-playback error (corrupt file, codec failure, storage device unmounted), the thread silently exits after a fade-out. The Dart side only sees `PlaybackState.stopped` via the native push callback — there is no error code, no error message, and no way to distinguish "track finished naturally" from "track died due to corruption."
- **Impact:** Apps cannot show meaningful error UI ("File corrupted, skipping...") or implement retry logic.
- **Fix:** Add an error field to the native push message (e.g., `[trackIndex, posMs, running, errorCode]`) and expose an `onError` stream on `TrackPlayer`.

### 30. Error Handling Returns Raw C Codes — No Dart-Friendly Errors

- **File:** `lib/src/track_player.dart` (`play()` returns `int`), `lib/src/audio_mixer.dart` (static methods)
- **Severity:** Low
- **Description:** All playback methods return `0 = success`, non-zero = failure with no further information. This is idiomatic in C but not in Dart. Callers cannot determine whether a failure was caused by a missing file, unsupported format, full track slot, or decoder error.
- **Suggestion:** Add documented error codes (e.g., `-1` = invalid path, `-2` = unsupported format, `-3` = track slot busy) or throw typed exceptions.

---

## What's Good

- **Clean layered architecture:** Dart → FFI → Dispatcher → Decoder → RingBuffer → AAudio with clear separation of concerns
- **Abstract `FfiInterface` for testability:** `FakeFfi` injection allows Dart unit tests without a real native library
- **Lock-free SPSC ring buffer:** No locks on the hot path between decoder and audio callback
- **eventfd signaling:** Efficient Linux kernel mechanism for stopping decoder threads
- **Well-documented codebase:** README in English/Spanish, detailed ROADMAP, CHANGELOG, CONTRIBUTING guide
- **Solid DSP implementation:** RBJ biquad EQ cookbook, proper limiter and compressor design
- **Thoughtful effect registry:** Strategy pattern with factory lambdas for extensible audio effects
- **Backward-compatible facade:** Static methods on `AudioEngine` delegate to track 0 for simple use cases

---

## Recommended Priority Order

| Priority | Issue # | Description | Status |
|----------|---------|-------------|--------|
| P0 | #1 | Replace all `volatile int` with `std::atomic<int>` in `TrackState` | ✅ Fixed |
| P0 | #2 | Remove mutex from audio callback path in `EffectChain` | ✅ Fixed |
| P0 | #3 | Replace heap allocation in `Reverb::process()` with stack allocation | ✅ Fixed |
| P0 | #4 | Register AAudio disconnect error callback | ✅ Fixed |
| P0 | #5 | Fix `cleanupEngine()` to delete `fxChain` | ✅ Fixed |
| P1 | #6 | Close `_nameCtrl` in `dispose()` | ✅ Fixed |
| P1 | #9 | Extract duplicated cleanup logic into `cleanupEngine()` | ✅ Fixed |
| P1 | #10 | Deduplicate WAV parsing in `track_play()` | ✅ Fixed |
| P1 | #12 | Replace VLAs with fixed-size buffers | ✅ Fixed |
| P2 | #7 | Implement native push for position updates | ✅ Fixed |
| P2 | #8 | Replace debug `print()` with proper logger | ⚠️ Accepted |
| P2 | #11 | Add sample rate mismatch detection/rejection | Pending |
| P2 | #13 | Add backpressure to `PcmStream` | Pending |
| P2 | #18 | Provide x86/x86_64 precompiled libs or document limitation | Pending |
| P3 | #14-17 | Clean up gitignore / remove committed artifacts | Pending |
| P3 | #19-28 | Long-term architectural improvements | Pending |
