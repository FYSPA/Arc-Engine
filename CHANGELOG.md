## 0.1.1 (Unreleased)

### Bug Fixes
- repeatCount=1 now stops instead of gapless-transitioning to next track
- Loop activation race condition fixed — seeking to start before checking loop flag
- WAV decode path restructured — gapless label moved after decoder init
- FLAC EOS no longer kills thread in loop mode — checks repeatCount before exiting
- UTF-8 metadata: accented characters (é, ñ, etc.) now display correctly
- Atomic volume/pan eliminates data races in audio callback
- Audio Local/Stream tracks now re-apply EQ chain on playback start
- Decoder errors now propagated to Dart via `onError` stream (#29)
- strncpy null-termination fix in gapless path (#27)

### Features
- Repeat count loop (Spotify-style): 0=off, -1=infinite, N=repeat N times
- Per-track EQ system: each track has own `DspProcessor` with optional global override
- Real limiter rewritten with envelope follower + look-ahead buffer
- WAV export: `exportToWav()` and `convertFileToWav()` for multi-track mix and single file conversion
- `onMetadataLoaded` stream on `TrackPlayer` for gapless transitions (#31)
- `onError` stream on `TrackPlayer` for decoder/codec errors (#29)

### Performance
- Pre-allocated `xmixBuf`/`xresampleBuf` in `TrackState` eliminate heap allocs in hot path
- FLAC crossfade uses `ensureCapacity()` instead of per-call `std::vector` allocation
- `applyFadeIn()` optimized — early return when no fade needed

---

## 0.1.0

### Features
- Multi-format playback: FLAC (libFLAC), WAV (native parser), MP3/AAC/OGG (AMediaCodec)
- Low-latency AAudio callback output with lock-free SPSC ring buffer
- Multi-track mixer: 4 simultaneous tracks with independent volume + constant-power pan
- Real-time 10-band DSP EQ (peaking, low/high shelf, low/high pass filters)
- URL streaming via AMediaExtractor (API 29+) with download-then-play fallback
- PCM stream to Dart for waveform visualization
- eventfd kernel-level cross-thread signaling
- File picker integration (SAF, bypasses scoped storage)

### Gapless & Crossfade
- Spotify-style equal-power crossfade (0–5s configurable via `AudioEngine.crossfadeMs`)
- Gapless transitions via `TrackPlayer.setNextTrack(path, {name})`
- SR mismatch handling: real-time sinc resampler with precomputed lookup table (256 bins × 17 taps)
- Pre-decode next track in background thread for seamless transitions
- Chunked crossfade push (192 frames/chunk) to prevent underruns during resampling
- Overlap buffer for block-continuous sinc resampling across callback boundaries

### Effects & Dynamics
- Post-EQ effect chain: compressor + reverb
- Compressor: threshold, ratio, knee, attack, release, makeup gain
- Reverb: 4 comb + 2 all-pass filters with pre-delay
- Post-mix limiter: hard-clipper with configurable threshold (-60 to 0 dB)

### Audio Focus & Hardware
- Bluetooth auto-pause: music pauses on headphone/BT disconnect, resumes on reconnect
- Audio focus: ducking, notification pause, becoming-noisy handler

### Native
- C++ engine: dispatcher, decoder threads (WAV/FLAC/Media/Stream), AAudio callback
- Biquad filter implementation (RBJ cookbook coefficients)
- Sinc lookup table for fast resampling (precomputed sinc×Hann weights)
- Input/output clipping in FLAC handler for out-of-range sample protection
- 22 C++ unit tests + latency benchmarks

### Dart
- AudioEngine singleton with static backward-compatible API
- TrackPlayer with Stream-based state/position notifications (250ms polling)
- PcmStream for real-time audio data access
- 30 Dart unit tests with mock FFI interface
