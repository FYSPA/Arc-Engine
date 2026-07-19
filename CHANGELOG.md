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

### Native
- C++ engine: dispatcher, decoder threads (WAV/FLAC/Media/Stream), AAudio callback
- Biquad filter implementation (RBJ cookbook coefficients)
- 22 C++ unit tests + latency benchmarks

### Dart
- AudioEngine singleton with static backward-compatible API
- TrackPlayer with Stream-based state/position notifications (250ms polling)
- PcmStream for real-time audio data access
- 30 Dart unit tests with mock FFI interface
