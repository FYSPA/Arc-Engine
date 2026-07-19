// ---------------------------------------------------------------------------
// File: pcm_stream.dart
// Purpose: Exposes a real-time Dart Stream<List<double>> of raw PCM float
//          samples decoded by the native engine. Useful for visualization
//          (waveform, VU meter) and analysis.
// Importance: Only way to access decoded audio data from Dart without
//             writing custom native code.
// Missing: - No backpressure control — if Dart consumer is slower than the
//            50ms interval, samples are dropped silently
//          - Buffer allocation (calloc) happens on every tick — should pool
// Known issues: The onCancel callback in StreamController sets _active=false
//               but does not cancel the Timer immediately.
// ---------------------------------------------------------------------------

import 'dart:async';
import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface;

/// Provides a real-time [Stream] of raw PCM float samples from the native
/// engine, useful for waveform visualization and audio analysis.
///
/// Samples are interleaved floats in the range -1.0 to 1.0.
/// The stream is a broadcast stream; multiple listeners are supported.
class PcmStream {
  final FfiInterface _ffi = FfiInterface.instance;
  StreamController<List<double>>? _ctrl;
  Timer? _timer;
  bool _active = false;

  /// Starts emitting PCM sample lists at [interval].
  ///
  /// Returns a broadcast [Stream] where each event is a [List<double>] of
  /// interleaved float samples (-1.0 to 1.0). Stops when [stop] or [dispose]
  /// is called, or when all listeners cancel.
  Stream<List<double>> start(
      {Duration interval = const Duration(milliseconds: 50)}) {
    _active = true;
    _ctrl = StreamController<List<double>>.broadcast(
      onCancel: () => _active = false,
    );

    _timer = Timer.periodic(interval, (_) {
      if (!_active) {
        _timer?.cancel();
        _timer = null;
        return;
      }
      final available = _ffi.getPcmAvailable();
      if (available <= 0) return;
      final frames = available > 512 ? 512 : available;
      final buffer = calloc<Float>(frames * 2);
      final samplesRead = _ffi.readPcmSamples(buffer, frames);
      if (samplesRead > 0) {
        final count = samplesRead < 1024 ? samplesRead : 1024;
        final samples =
            List<double>.generate(count, (i) => buffer[i].toDouble());
        _ctrl?.add(samples);
      }
      calloc.free(buffer);
    });

    return _ctrl!.stream;
  }

  /// Stops the PCM stream and closes the stream controller.
  void stop() {
    _active = false;
    _timer?.cancel();
    _timer = null;
    _ctrl?.close();
    _ctrl = null;
  }

  /// Alias for [stop]. Releases resources.
  void dispose() => stop();
}
