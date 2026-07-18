import 'dart:async';
import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart' show FfiInterface;

class PcmStream {
  final FfiInterface _ffi = FfiInterface.instance;
  StreamController<List<double>>? _ctrl;
  Timer? _timer;
  bool _active = false;

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

  void stop() {
    _active = false;
    _timer?.cancel();
    _timer = null;
    _ctrl?.close();
    _ctrl = null;
  }

  void dispose() => stop();
}
