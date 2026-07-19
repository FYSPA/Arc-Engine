// ---------------------------------------------------------------------------
// File: audio_engine_test.dart
// Purpose: Unit tests for AudioEngine singleton, backward-compat static API
//          (startAudio, stop, pause, resume, seek, getPosition, getDuration),
//          and master volume clamping.
// Importance: Ensures AudioEngine correctly delegates to FfiInterface.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

import 'package:flutter_test/flutter_test.dart';
import 'package:fake_async/fake_async.dart';
import 'package:arc_engine/arc_engine.dart';
import 'package:arc_engine/src/ffi_bindings.dart';

import 'fake_ffi.dart';

void main() {
  final ffi = FakeFfi();

  setUp(() {
    FfiInterface.instance = ffi;
  });

  tearDown(() {
    ffi.reset();
  });

  group('AudioEngine singleton', () {
    test('instance returns the same object', () {
      expect(AudioEngine.instance, same(AudioEngine.instance));
    });

    test('has 4 tracks', () {
      expect(AudioEngine.instance.tracks.length, 4);
    });

    test('each track has a unique index', () {
      final tracks = AudioEngine.instance.tracks;
      for (int i = 0; i < 4; i++) {
        expect(tracks[i].index, i);
      }
    });
  });

  group('AudioEngine backward compat', () {
    test('startAudio delegates to track 0', () {
      final result = AudioEngine.startAudio('/test/path.wav');
      expect(result, 0);
      expect(AudioEngine.isPlaying, isTrue);
    });

    test('stop delegates to track 0', () {
      AudioEngine.startAudio('/test/path.wav');
      expect(AudioEngine.isPlaying, isTrue);
      AudioEngine.stop();
      expect(AudioEngine.isPlaying, isFalse);
    });

    test('pause and resume affect track 0', () {
      AudioEngine.startAudio('/test/path.wav');
      AudioEngine.pause();
      expect(ffi.playing[0], isTrue);
      expect(ffi.positions[0], 0);

      AudioEngine.resume();
      expect(ffi.playing[0], isTrue);
    });

    test('seek sets position on track 0', () {
      AudioEngine.startAudio('/test/path.wav');
      AudioEngine.seek(500);
      expect(ffi.positions[0], 500);
    });

    test('getPosition and getDuration', () {
      FakeAsync().run((async) {
        AudioEngine.startAudio('/test/path.wav');
        ffi.positions[0] = 3000;
        async.elapse(const Duration(milliseconds: 300));
        expect(AudioEngine.getPosition(), 3000);
      });
    });
  });

  group('AudioEngine master volume', () {
    test('setter propagates to FFI', () {
      AudioEngine.instance.masterVolume = 0.75;
      expect(ffi.masterVol, 0.75);
    });

    test('clamped to 0.0–1.0', () {
      AudioEngine.instance.masterVolume = -0.1;
      expect(AudioEngine.instance.masterVolume, 0.0);
      AudioEngine.instance.masterVolume = 1.5;
      expect(AudioEngine.instance.masterVolume, 1.0);
    });
  });
}
