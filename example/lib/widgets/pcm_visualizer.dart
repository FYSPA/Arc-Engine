import 'dart:async';

import 'package:flutter/material.dart';
import 'package:audio_engine/audio_engine.dart';

class PcmVisualizer extends StatefulWidget {
  const PcmVisualizer({super.key});

  @override
  State<PcmVisualizer> createState() => _PcmVisualizerState();
}

class _PcmVisualizerState extends State<PcmVisualizer> {
  StreamSubscription<List<double>>? _subscription;
  final List<double> _waveform = [];
  static const int _waveformLen = 60;

  @override
  void initState() {
    super.initState();
    _startStream();
  }

  void _startStream() {
    final stream =
        AudioEngine.startPcmStream(interval: const Duration(milliseconds: 50));
    _subscription = stream.listen((samples) {
      if (!mounted) return;
      setState(() {
        _waveform.addAll(samples);
        while (_waveform.length > _waveformLen) {
          _waveform.removeAt(0);
        }
      });
    });
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Waveform',
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: Colors.white.withValues(alpha: 0.5),
              ),
            ),
            const SizedBox(height: 12),
            ClipRRect(
              borderRadius: BorderRadius.circular(6),
              child: Container(
                height: 48,
                color: Colors.white.withValues(alpha: 0.03),
                child: CustomPaint(
                  size: Size.infinite,
                  painter: _WaveformPainter(_waveform),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _WaveformPainter extends CustomPainter {
  final List<double> samples;

  _WaveformPainter(this.samples);

  @override
  void paint(Canvas canvas, Size size) {
    if (samples.isEmpty) return;

    final paint = Paint()
      ..color = const Color(0xFF7C4DFF).withValues(alpha: 0.4)
      ..strokeWidth = 1.5
      ..strokeCap = StrokeCap.round;

    final centerY = size.height / 2;
    final stepX = size.width / samples.length;

    for (int i = 0; i < samples.length - 1; i++) {
      final x1 = i * stepX;
      final y1 = centerY + (samples[i].clamp(-1.0, 1.0) * (size.height / 2 - 4));
      final x2 = (i + 1) * stepX;
      final y2 = centerY + (samples[i + 1].clamp(-1.0, 1.0) * (size.height / 2 - 4));
      canvas.drawLine(Offset(x1, y1), Offset(x2, y2), paint);
    }
  }

  @override
  bool shouldRepaint(_WaveformPainter oldDelegate) =>
      oldDelegate.samples != samples;
}
