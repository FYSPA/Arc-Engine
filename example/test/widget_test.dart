import 'package:flutter_test/flutter_test.dart';

import 'package:audio_engine_example/widgets/app.dart';

void main() {
  testWidgets('App loads without errors', (WidgetTester tester) async {
    await tester.pumpWidget(const App());

    // Verify the app title is present
    expect(find.text('Audio Engine'), findsOneWidget);
  });
}
