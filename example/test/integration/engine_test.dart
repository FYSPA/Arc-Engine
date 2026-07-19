import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:arc_engine/arc_engine.dart';
import 'package:arc_engine_example/widgets/app.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  group('AudioEngine integration', () {
    test('native library loads', () {
      // getDuration returns 0 when nothing is playing (negative on error)
      expect(AudioEngine.getDuration(), greaterThanOrEqualTo(0));
    });

    test('master volume clamp works', () {
      final engine = AudioEngine.instance;
      engine.masterVolume = 0.5;
      expect(engine.masterVolume, 0.5);
      engine.masterVolume = 1.5;
      expect(engine.masterVolume, 1.0);
      engine.masterVolume = -0.1;
      expect(engine.masterVolume, 0.0);
    });

    test('tracks are accessible and have correct indices', () {
      final tracks = AudioEngine.instance.tracks;
      expect(tracks.length, 4);
      for (int i = 0; i < 4; i++) {
        expect(tracks[i].index, i);
      }
    });

    test('stop is safe when nothing is playing', () {
      AudioEngine.stop();
      expect(AudioEngine.isPlaying, isFalse);
    });

    testWidgets('App widget renders without errors',
        (WidgetTester tester) async {
      await tester.pumpWidget(const App());
      expect(find.text('Audio Engine'), findsOneWidget);
    });
  });
}
