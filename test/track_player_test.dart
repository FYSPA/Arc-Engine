import 'package:flutter_test/flutter_test.dart';
import 'package:fake_async/fake_async.dart';
import 'package:audio_engine/audio_engine.dart';
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

  group('TrackPlayer', () {
    test('initial state is stopped', () {
      final tp = TrackPlayer(0);
      expect(tp.state, PlaybackState.stopped);
      expect(tp.position.inMilliseconds, 0);
      expect(tp.volume, 1.0);
      expect(tp.pan, 0.0);
    });

    test('play transitions to playing', () {
      final tp = TrackPlayer(0);
      final result = tp.play('/test/path.wav');
      expect(result, 0);
      expect(tp.state, PlaybackState.playing);
    });

    test('stop transitions to stopped', () {
      final tp = TrackPlayer(0);
      tp.play('/test/path.wav');
      expect(tp.state, PlaybackState.playing);

      tp.stop();
      expect(tp.state, PlaybackState.stopped);
      expect(tp.position.inMilliseconds, 0);
    });

    test('pause and resume toggle state', () {
      final tp = TrackPlayer(0);
      tp.play('/test/path.wav');

      tp.pause();
      expect(tp.state, PlaybackState.paused);

      tp.resume();
      expect(tp.state, PlaybackState.playing);
    });

    test('play on already playing track calls stop first', () {
      ffi.playing[0] = true;
      ffi.positions[0] = 5000;

      final tp = TrackPlayer(0);
      tp.play('/test/other.wav');

      expect(tp.position.inMilliseconds, 0);
      expect(tp.state, PlaybackState.playing);
    });

    test('volume setter propagates to FFI', () {
      final tp = TrackPlayer(0);
      tp.play('/test/path.wav');

      tp.volume = 0.5;
      expect(tp.volume, 0.5);
      expect(ffi.volumes[0], 0.5);
    });

    test('pan setter propagates to FFI', () {
      final tp = TrackPlayer(0);
      tp.play('/test/path.wav');

      tp.pan = -0.8;
      expect(tp.pan, -0.8);
      expect(ffi.pans[0], -0.8);
    });

    test('volume is clamped to 0.0–1.0', () {
      final tp = TrackPlayer(0);
      tp.volume = -0.1;
      expect(tp.volume, 0.0);
      tp.volume = 1.5;
      expect(tp.volume, 1.0);
    });

    test('pan is clamped to -1.0–1.0', () {
      final tp = TrackPlayer(0);
      tp.pan = -2.0;
      expect(tp.pan, -1.0);
      tp.pan = 2.0;
      expect(tp.pan, 1.0);
    });

    test('onStateChanged emits on state transitions', () async {
      final tp = TrackPlayer(0);
      final states = <PlaybackState>[];
      tp.onStateChanged.listen((s) => states.add(s));

      tp.play('/test/path.wav');
      await Future(() {});
      expect(states.contains(PlaybackState.playing), isTrue);
    });

    test('onPositionChanged emits on seek', () async {
      final tp = TrackPlayer(0);
      final positions = <int>[];
      tp.onPositionChanged.listen((p) => positions.add(p.inMilliseconds));

      tp.seek(const Duration(milliseconds: 500));
      await Future(() {});
      expect(positions.contains(500), isTrue);
    });

    test('seek calls FFI and updates position', () {
      final tp = TrackPlayer(0);
      tp.seek(const Duration(milliseconds: 300));
      expect(ffi.positions[0], 300);
      expect(tp.position.inMilliseconds, 300);
    });

    test('Timer polling updates position from FFI', () {
      // Use fakeAsync to control Timer.periodic
      FakeAsync().run((async) {
        final tp = TrackPlayer(0);
        tp.play('/test/path.wav');
        expect(tp.state, PlaybackState.playing);

        // Advance time so timer fires and simulates playback
        ffi.positions[0] = 500;
        async.elapse(const Duration(milliseconds: 300));
        expect(tp.position.inMilliseconds, 500);
      });
    });

    test('dispose stops and closes streams', () {
      final tp = TrackPlayer(0);
      tp.play('/test/path.wav');
      tp.dispose();
      expect(tp.state, PlaybackState.stopped);
    });
  });
}
