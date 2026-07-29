import 'ffi_bindings.dart' show FfiInterface;

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

  static final List<double> gains = List.filled(10, 0.0);
  static final List<int> types = List.filled(10, peaking);
  static final List<double> qs = List.filled(10, 0.707);
  static final List<bool> enabled = List.filled(10, false);
  static bool bypass = false;

  /// Re-applies all stored EQ settings via FFI.
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
}
