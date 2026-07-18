import 'package:flutter_test/flutter_test.dart';
import 'package:fake_async/fake_async.dart';
import 'package:audio_engine/src/pcm_stream.dart';
import 'package:audio_engine/src/ffi_bindings.dart';

import 'fake_ffi.dart';

void main() {
  final ffi = FakeFfi();

  setUp(() {
    FfiInterface.instance = ffi;
  });

  tearDown(() {
    ffi.reset();
  });

  group('PcmStream', () {
    test('constructor does not throw', () {
      final ps = PcmStream();
      expect(ps, isNotNull);
    });

    test('start returns a stream that emits samples at interval', () {
      FakeAsync().run((async) {
        ffi.pcmAvailable = 256;
        ffi.pcmSamples = List<double>.generate(512, (i) => i.toDouble());

        final ps = PcmStream();
        final emitted = <List<double>>[];
        ps.start().listen((samples) => emitted.add(samples));

        async.elapse(const Duration(milliseconds: 50));
        expect(emitted.length, 1);
        expect(emitted[0].length, 256);
      });
    });

    test('emits multiple times when samples remain available', () {
      FakeAsync().run((async) {
        ffi.pcmAvailable = 256;
        ffi.pcmSamples = List<double>.generate(512, (i) => i.toDouble());

        final ps = PcmStream();
        final emitted = <List<double>>[];
        ps.start().listen((samples) => emitted.add(samples));

        async.elapse(const Duration(milliseconds: 150));
        expect(emitted.length, 3);
      });
    });

    test('stop cancels the stream', () {
      FakeAsync().run((async) {
        ffi.pcmAvailable = 256;
        ffi.pcmSamples = List<double>.generate(512, (i) => i.toDouble());

        final ps = PcmStream();
        final emitted = <List<double>>[];
        ps.start().listen((samples) => emitted.add(samples));

        async.elapse(const Duration(milliseconds: 60));
        ps.stop();

        final countBefore = emitted.length;
        async.elapse(const Duration(milliseconds: 200));
        expect(emitted.length, countBefore);
      });
    });

    test('does not emit when no samples available', () {
      FakeAsync().run((async) {
        ffi.pcmAvailable = 0;

        final ps = PcmStream();
        final emitted = <List<double>>[];
        ps.start().listen((samples) => emitted.add(samples));

        async.elapse(const Duration(milliseconds: 200));
        expect(emitted, isEmpty);
      });
    });

    test('dispose stops the stream', () {
      FakeAsync().run((async) {
        ffi.pcmAvailable = 256;
        ffi.pcmSamples = List<double>.generate(512, (i) => i.toDouble());

        final ps = PcmStream();
        final emitted = <List<double>>[];
        ps.start().listen((samples) => emitted.add(samples));

        async.elapse(const Duration(milliseconds: 60));
        ps.dispose();

        final countBefore = emitted.length;
        async.elapse(const Duration(milliseconds: 200));
        expect(emitted.length, countBefore);
      });
    });
  });
}
