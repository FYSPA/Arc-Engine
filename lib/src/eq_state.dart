import 'ffi_bindings.dart' show FfiInterface;

/// Per-track EQ state (override for a specific track).
class EqTrackState {
  final List<double> gains;
  final List<int> types;
  final List<double> qs;
  final List<bool> enabled;
  bool bypass;

  EqTrackState({
    List<double>? gains,
    List<int>? types,
    List<double>? qs,
    List<bool>? enabled,
    this.bypass = false,
  })  : gains = gains ?? List.filled(10, 0.0),
        types = types ?? List.filled(10, EqState.peaking),
        qs = qs ?? List.filled(10, 0.707),
        enabled = enabled ?? List.filled(10, false);

  /// Re-applies all stored per-track EQ settings via FFI.
  void apply(int trackIndex) {
    final ffi = FfiInterface.instance;
    for (int i = 0; i < 10; i++) {
      ffi.eqSetTrackBand(
          trackIndex, i, types[i], EqState.bandFrequencies[i], gains[i], qs[i]);
      ffi.eqSetTrackBandEnabled(trackIndex, i, enabled[i] ? 1 : 0);
    }
    ffi.eqSetTrackBypass(trackIndex, bypass ? 1 : 0);
  }

  void reset() {
    for (int i = 0; i < 10; i++) {
      gains[i] = 0.0;
      types[i] = EqState.peaking;
      qs[i] = 0.707;
      enabled[i] = false;
    }
    bypass = false;
  }
}

/// Persistent EQ state that survives across track play/stop cycles.
/// Shared between AudioEngine (setters) and TrackPlayer (re-apply on play).
class EqState {
  static const int peaking = 0;
  static const int lowShelf = 1;
  static const int highShelf = 2;
  static const int lowPass = 3;
  static const int highPass = 4;

  static const List<double> bandFrequencies = [
    31.0,
    62.0,
    125.0,
    250.0,
    500.0,
    1000.0,
    2000.0,
    4000.0,
    8000.0,
    16000.0,
  ];

  // Global EQ state
  static final List<double> gains = List.filled(10, 0.0);
  static final List<int> types = List.filled(10, peaking);
  static final List<double> qs = List.filled(10, 0.707);
  static final List<bool> enabled = List.filled(10, false);
  static bool bypass = false;

  // Per-track EQ overrides (trackIndex -> EqTrackState)
  static final Map<int, EqTrackState> trackOverrides = {};

  /// Re-applies all stored global EQ settings via FFI.
  /// Called after track starts playing (when gCtl.dsp is guaranteed to exist).
  static void apply() {
    final ffi = FfiInterface.instance;
    for (int i = 0; i < 10; i++) {
      ffi.eqSetBand(i, types[i], bandFrequencies[i], gains[i], qs[i]);
      ffi.eqSetBandEnabled(i, enabled[i] ? 1 : 0);
    }
    ffi.eqSetBypass(bypass ? 1 : 0);
  }

  static void reset() {
    for (int i = 0; i < 10; i++) {
      gains[i] = 0.0;
      types[i] = peaking;
      qs[i] = 0.707;
      enabled[i] = false;
    }
    bypass = false;
  }

  // ─── Per-track EQ helpers ──────────────────────────────────────────

  /// Gets or creates the per-track EQ state for a given track.
  static EqTrackState getTrackState(int trackIndex) {
    return trackOverrides.putIfAbsent(trackIndex, () => EqTrackState());
  }

  /// Removes per-track EQ override for a track (reverts to global).
  static void clearTrack(int trackIndex) {
    trackOverrides.remove(trackIndex);
    FfiInterface.instance.eqClearTrack(trackIndex);
  }

  /// Resets per-track EQ to flat (but keeps the override active).
  static void resetTrack(int trackIndex) {
    final state = trackOverrides[trackIndex];
    if (state != null) state.reset();
    FfiInterface.instance.eqResetTrack(trackIndex);
  }

  /// Sets a per-track EQ band.
  static void setTrackBand(int trackIndex, int bandIndex, int type, double freq,
      double gain, double q) {
    final state = getTrackState(trackIndex);
    state.gains[bandIndex] = gain;
    state.types[bandIndex] = type;
    state.qs[bandIndex] = q;
    state.enabled[bandIndex] = gain != 0.0;
    FfiInterface.instance
        .eqSetTrackBand(trackIndex, bandIndex, type, freq, gain, q);
    FfiInterface.instance.eqSetTrackBandEnabled(
        trackIndex, bandIndex, state.enabled[bandIndex] ? 1 : 0);
  }
}
