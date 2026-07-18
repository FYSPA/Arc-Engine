import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:audio_engine_example/widgets/app.dart';

void main() {
  testWidgets('App loads without errors', (WidgetTester tester) async {
    await tester.pumpWidget(const App());

    // Verify the app title is present
    expect(find.text('Audio Engine'), findsOneWidget);

    // Replace widget to trigger dispose, then flush pending timers
    await tester.pumpWidget(const SizedBox());
    await tester.pump(const Duration(milliseconds: 300));
  });
}
