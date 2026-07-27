import 'dart:math';
import 'package:flutter/material.dart';

class StaticWaveformWidget extends StatelessWidget {
  final List<double> peaks;
  final double position;
  final Color color;
  final Color playedColor;
  final double height;
  final ValueChanged<double>? onSeek;

  const StaticWaveformWidget({
    super.key,
    required this.peaks,
    this.position = 0.0,
    this.color = const Color(0xFF7C4DFF),
    this.playedColor = const Color(0xFF7C4DFF),
    this.height = 48,
    this.onSeek,
  });

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        return GestureDetector(
          onHorizontalDragStart: onSeek != null
              ? (d) => _emitSeek(context, d.globalPosition)
              : null,
          onHorizontalDragUpdate: onSeek != null
              ? (d) => _emitSeek(context, d.globalPosition)
              : null,
          onTapUp: onSeek != null
              ? (d) => _emitSeek(context, d.globalPosition)
              : null,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(6),
            child: Container(
              height: height,
              color: Colors.white.withValues(alpha: 0.03),
              child: CustomPaint(
                size: Size.infinite,
                painter: _StaticWaveformPainter(
                  peaks: peaks,
                  position: position.clamp(0.0, 1.0),
                  color: color,
                  playedColor: playedColor,
                ),
              ),
            ),
          ),
        );
      },
    );
  }

  void _emitSeek(BuildContext context, Offset globalPosition) {
    final renderBox = context.findRenderObject() as RenderBox?;
    if (renderBox == null) return;
    final local = renderBox.globalToLocal(globalPosition);
    final x = local.dx.clamp(0.0, renderBox.size.width);
    onSeek!.call(x / renderBox.size.width);
  }
}

class _StaticWaveformPainter extends CustomPainter {
  final List<double> peaks;
  final double position;
  final Color color;
  final Color playedColor;

  _StaticWaveformPainter({
    required this.peaks,
    required this.position,
    required this.color,
    required this.playedColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (peaks.isEmpty) return;

    final centerY = size.height / 2;
    final numBars = peaks.length;
    final barWidth = size.width / numBars;
    final maxBarHeight = size.height / 2 - 2;

    final playedPaint = Paint()..color = playedColor.withValues(alpha: 0.85);
    final normalPaint = Paint()..color = color.withValues(alpha: 0.35);

    canvas.drawLine(
      Offset(0, centerY),
      Offset(size.width, centerY),
      Paint()
        ..color = Colors.white.withValues(alpha: 0.06)
        ..strokeWidth = 1,
    );

    final playX = position * size.width;

    for (int i = 0; i < numBars; i++) {
      final x = i * barWidth;
      final peak = peaks[i].clamp(0.0, 1.0);
      final barH = max(maxBarHeight * peak, 1.0);
      final paint = (x + barWidth / 2 < playX) ? playedPaint : normalPaint;

      canvas.drawRect(
        Rect.fromLTWH(x + 1, centerY - barH, max(barWidth - 2, 1), barH),
        paint,
      );
      canvas.drawRect(
        Rect.fromLTWH(x + 1, centerY, max(barWidth - 2, 1), barH),
        paint,
      );
    }

    canvas.drawLine(
      Offset(playX, 0),
      Offset(playX, size.height),
      Paint()
        ..color = Colors.white
        ..strokeWidth = 1.5,
    );
  }

  @override
  bool shouldRepaint(_StaticWaveformPainter oldDelegate) =>
      oldDelegate.position != position || oldDelegate.peaks != peaks;
}
