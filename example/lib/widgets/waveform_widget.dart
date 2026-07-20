import 'package:flutter/material.dart';

class WaveformWidget extends StatelessWidget {
  final List<double> samples;
  final Color color;
  final double height;

  const WaveformWidget({
    super.key,
    required this.samples,
    this.color = const Color(0xFF7C4DFF),
    this.height = 48,
  });

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(6),
      child: Container(
        height: height,
        color: Colors.white.withValues(alpha: 0.03),
        child: CustomPaint(
          size: Size.infinite,
          painter: _WaveformPainter(samples, color),
        ),
      ),
    );
  }
}

class _WaveformPainter extends CustomPainter {
  final List<double> samples;
  final Color color;

  _WaveformPainter(this.samples, this.color);

  @override
  void paint(Canvas canvas, Size size) {
    if (samples.isEmpty) return;

    final centerY = size.height / 2;
    final stepX = size.width / samples.length;

    final linePaint = Paint()
      ..color = color.withValues(alpha: 0.6)
      ..strokeWidth = 1.5
      ..strokeCap = StrokeCap.round;

    final fillPaint = Paint()
      ..shader = LinearGradient(
        begin: Alignment.topCenter,
        end: Alignment.bottomCenter,
        colors: [
          color.withValues(alpha: 0.25),
          color.withValues(alpha: 0.0),
        ],
      ).createShader(Rect.fromLTWH(0, 0, size.width, size.height));

    final path = Path();
    bool first = true;

    for (int i = 0; i < samples.length; i++) {
      final x = i * stepX;
      final y = centerY + (samples[i].clamp(-1.0, 1.0) * (size.height / 2 - 4));
      if (first) {
        path.moveTo(x, centerY);
        path.lineTo(x, y);
        first = false;
      } else {
        path.lineTo(x, y);
      }
    }

    // Draw fill
    path.lineTo(size.width, centerY);
    path.close();
    canvas.drawPath(path, fillPaint);

    // Draw center line
    canvas.drawLine(
      Offset(0, centerY),
      Offset(size.width, centerY),
      Paint()
        ..color = Colors.white.withValues(alpha: 0.06)
        ..strokeWidth = 1,
    );

    // Draw waveform line
    for (int i = 0; i < samples.length - 1; i++) {
      final x1 = i * stepX;
      final y1 =
          centerY + (samples[i].clamp(-1.0, 1.0) * (size.height / 2 - 4));
      final x2 = (i + 1) * stepX;
      final y2 =
          centerY + (samples[i + 1].clamp(-1.0, 1.0) * (size.height / 2 - 4));
      canvas.drawLine(Offset(x1, y1), Offset(x2, y2), linePaint);
    }
  }

  @override
  bool shouldRepaint(_WaveformPainter oldDelegate) =>
      oldDelegate.samples != samples;
}
