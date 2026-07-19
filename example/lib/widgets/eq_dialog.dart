import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:arc_engine/arc_engine.dart';

const _sr = 44100.0;

double _bandResponse(int type, double freq, double gain, double q, double f) {
  final w0 = 2 * math.pi * freq / _sr;
  final cosW = math.cos(w0);
  final sinW = math.sin(w0);
  final alpha = sinW / (2 * q);
  final A = math.pow(10, gain / 40.0);

  double b0, b1, b2, a0, a1, a2;

  switch (type) {
    case AudioEngine.eqPeaking:
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cosW;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cosW;
      a2 = 1.0 - alpha / A;
    case AudioEngine.eqLowShelf:
      b0 = A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * math.sqrt(A) * alpha);
      b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
      b2 = A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * math.sqrt(A) * alpha);
      a0 = (A + 1.0) + (A - 1.0) * cosW + 2.0 * math.sqrt(A) * alpha;
      a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
      a2 = (A + 1.0) + (A - 1.0) * cosW - 2.0 * math.sqrt(A) * alpha;
    case AudioEngine.eqHighShelf:
      b0 = A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * math.sqrt(A) * alpha);
      b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
      b2 = A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * math.sqrt(A) * alpha);
      a0 = (A + 1.0) - (A - 1.0) * cosW + 2.0 * math.sqrt(A) * alpha;
      a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW);
      a2 = (A + 1.0) - (A - 1.0) * cosW - 2.0 * math.sqrt(A) * alpha;
    case AudioEngine.eqLowPass:
      b0 = (1.0 - cosW) / 2.0;
      b1 = 1.0 - cosW;
      b2 = (1.0 - cosW) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosW;
      a2 = 1.0 - alpha;
    case AudioEngine.eqHighPass:
      b0 = (1.0 + cosW) / 2.0;
      b1 = -(1.0 + cosW);
      b2 = (1.0 + cosW) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosW;
      a2 = 1.0 - alpha;
    default:
      return 0.0;
  }

  b0 /= a0;
  b1 /= a0;
  b2 /= a0;
  a1 /= a0;
  a2 /= a0;

  final w = 2 * math.pi * f / _sr;
  final cw = math.cos(w);
  final c2w = math.cos(2 * w);
  final sw = math.sin(w);
  final s2w = math.sin(2 * w);

  final numRe = b0 + b1 * cw + b2 * c2w;
  final numIm = b1 * sw + b2 * s2w;
  final denRe = 1.0 + a1 * cw + a2 * c2w;
  final denIm = a1 * sw + a2 * s2w;

  final magSq =
      (numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm);
  return 10.0 * math.log(magSq) / math.ln10;
}

const _numCurvePoints = 200;

const _typeLabels = ['Pk', 'LS', 'HS', 'LP', 'HP'];
const _typeNames = [
  'Peaking',
  'Low Shelf',
  'High Shelf',
  'Low Pass',
  'High Pass'
];

const _filterTypes = [
  AudioEngine.eqPeaking,
  AudioEngine.eqLowShelf,
  AudioEngine.eqHighShelf,
  AudioEngine.eqLowPass,
  AudioEngine.eqHighPass,
];

class EqDialog extends StatefulWidget {
  const EqDialog({super.key});

  @override
  State<EqDialog> createState() => _EqDialogState();
}

class _EqDialogState extends State<EqDialog> {
  bool _bypass = false;
  int _expandedIndex = -1;

  static const _bands = [
    (freq: 31.0, label: '31 Hz'),
    (freq: 62.0, label: '62 Hz'),
    (freq: 125.0, label: '125 Hz'),
    (freq: 250.0, label: '250 Hz'),
    (freq: 500.0, label: '500 Hz'),
    (freq: 1000.0, label: '1 kHz'),
    (freq: 2000.0, label: '2 kHz'),
    (freq: 4000.0, label: '4 kHz'),
    (freq: 8000.0, label: '8 kHz'),
    (freq: 16000.0, label: '16 kHz'),
  ];

  final List<double> _gains = List.filled(10, 0.0);
  final List<double> _qs = List.filled(10, 0.707);
  final List<int> _types = List.filled(10, AudioEngine.eqPeaking);
  final List<bool> _enabled = List.filled(10, false);

  void _updateBand(int index) {
    _enabled[index] = _gains[index] != 0.0;
    AudioEngine.setEqBand(
        index, _types[index], _bands[index].freq, _gains[index], _qs[index]);
    AudioEngine.setEqBandEnabled(index, _enabled[index]);
  }

  void _reset() {
    setState(() {
      for (int i = 0; i < 10; i++) {
        _gains[i] = 0.0;
        _qs[i] = 0.707;
        _types[i] = AudioEngine.eqPeaking;
        _enabled[i] = false;
      }
      _bypass = false;
      _expandedIndex = -1;
    });
    AudioEngine.resetEq();
    AudioEngine.setEqBypass(false);
  }

  void _toggleBypass(bool v) {
    setState(() => _bypass = v);
    AudioEngine.setEqBypass(v);
  }

  List<Offset> _computeCurve(Size size) {
    const padL = 44.0, padR = 12.0, padT = 16.0, padB = 28.0;
    const plotL = padL;
    final plotR = size.width - padR;
    const plotT = padT;
    final plotB = size.height - padB;
    final plotW = plotR - plotL;
    final plotH = plotB - plotT;

    final points = <Offset>[];
    for (int i = 0; i < _numCurvePoints; i++) {
      final t = i / (_numCurvePoints - 1);
      final freq = 20.0 * math.pow(1000.0, t);
      double totalDb = 0.0;
      for (int b = 0; b < 10; b++) {
        if (!_enabled[b]) continue;
        totalDb +=
            _bandResponse(_types[b], _bands[b].freq, _gains[b], _qs[b], freq);
      }
      final x = plotL + t * plotW;
      final yNorm = (totalDb + 12.0) / 24.0;
      final y = plotB - yNorm * plotH;
      points.add(Offset(x, y.clamp(plotT, plotB)));
    }
    return points;
  }

  Widget _buildCurve(Size size) {
    final points = _computeCurve(size);
    return ClipRect(
      child: CustomPaint(
        size: size,
        painter: _EqCurvePainter(points, _bands, _enabled, _gains),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: const Color(0xFF1A1A24),
      titlePadding: const EdgeInsets.fromLTRB(20, 16, 20, 0),
      contentPadding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
      actionsPadding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
      title: Row(
        children: [
          const Icon(Icons.tune_rounded, size: 18, color: Colors.white70),
          const SizedBox(width: 8),
          Text('Equalizer',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                  color: Colors.white.withValues(alpha: 0.9))),
          const Spacer(),
          SizedBox(
            height: 24,
            child: TextButton(
              onPressed: _reset,
              style: TextButton.styleFrom(
                padding: const EdgeInsets.symmetric(horizontal: 8),
                minimumSize: Size.zero,
                foregroundColor: const Color(0xFFEF5350),
              ),
              child: const Text('Reset', style: TextStyle(fontSize: 11)),
            ),
          ),
        ],
      ),
      content: SizedBox(
        width: double.maxFinite,
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              LayoutBuilder(
                builder: (_, constraints) => SizedBox(
                  height: 140,
                  width: constraints.maxWidth,
                  child: _buildCurve(constraints.biggest),
                ),
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  Text('Bypass',
                      style: TextStyle(
                          fontSize: 12,
                          color: Colors.white.withValues(alpha: 0.6))),
                  const SizedBox(width: 8),
                  SizedBox(
                    height: 24,
                    child: Switch.adaptive(
                      value: _bypass,
                      onChanged: _toggleBypass,
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              const Divider(height: 1, color: Colors.white12),
              const SizedBox(height: 4),
              ListBody(
                children: List.generate(10, (i) {
                  final isExpanded = _expandedIndex == i;
                  return Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      GestureDetector(
                        onTap: () => setState(
                            () => _expandedIndex = isExpanded ? -1 : i),
                        child: Padding(
                          padding: const EdgeInsets.symmetric(vertical: 2),
                          child: Row(
                            children: [
                              SizedBox(
                                width: 38,
                                child: Row(
                                  children: [
                                    Container(
                                      width: 4,
                                      height: 4,
                                      margin: const EdgeInsets.only(right: 4),
                                      decoration: BoxDecoration(
                                        shape: BoxShape.circle,
                                        color: _enabled[i]
                                            ? (_gains[i] >= 0
                                                ? const Color(0xFF4CAF50)
                                                : const Color(0xFFEF5350))
                                            : Colors.white
                                                .withValues(alpha: 0.12),
                                      ),
                                    ),
                                    Text(
                                      _bands[i].label,
                                      style: TextStyle(
                                        fontSize: 10,
                                        color: _enabled[i]
                                            ? Colors.white
                                                .withValues(alpha: 0.75)
                                            : Colors.white
                                                .withValues(alpha: 0.25),
                                        fontFamily: 'monospace',
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                              SizedBox(
                                width: 24,
                                height: 18,
                                child: Container(
                                  alignment: Alignment.center,
                                  decoration: BoxDecoration(
                                    color: _enabled[i]
                                        ? Colors.white.withValues(alpha: 0.08)
                                        : Colors.transparent,
                                    borderRadius: BorderRadius.circular(2),
                                  ),
                                  child: Text(
                                    _typeLabels[_types[i]],
                                    style: TextStyle(
                                      fontSize: 8,
                                      fontFamily: 'monospace',
                                      color: _enabled[i]
                                          ? Colors.white.withValues(alpha: 0.5)
                                          : Colors.white
                                              .withValues(alpha: 0.18),
                                    ),
                                  ),
                                ),
                              ),
                              const SizedBox(width: 4),
                              Expanded(
                                child: SizedBox(
                                  height: 24,
                                  child: SliderTheme(
                                    data: SliderTheme.of(context).copyWith(
                                      trackHeight: 2,
                                      thumbShape: const RoundSliderThumbShape(
                                          enabledThumbRadius: 6),
                                      overlayShape:
                                          const RoundSliderOverlayShape(
                                              overlayRadius: 10),
                                      activeTrackColor: _gains[i] >= 0
                                          ? const Color(0xFF4CAF50)
                                          : const Color(0xFFEF5350),
                                      inactiveTrackColor:
                                          Colors.white.withValues(alpha: 0.08),
                                      thumbColor: _gains[i] >= 0
                                          ? const Color(0xFF4CAF50)
                                          : const Color(0xFFEF5350),
                                      overlayColor: (_gains[i] >= 0
                                              ? const Color(0xFF4CAF50)
                                              : const Color(0xFFEF5350))
                                          .withValues(alpha: 0.08),
                                    ),
                                    child: Slider(
                                      value: _gains[i],
                                      min: -12.0,
                                      max: 12.0,
                                      divisions: 24,
                                      onChanged: (v) => setState(() {
                                        _gains[i] = v;
                                        _updateBand(i);
                                      }),
                                    ),
                                  ),
                                ),
                              ),
                              SizedBox(
                                width: 30,
                                child: Text(
                                  '${_gains[i].toStringAsFixed(0)} dB',
                                  style: TextStyle(
                                    fontSize: 9,
                                    color: _enabled[i]
                                        ? Colors.white.withValues(alpha: 0.5)
                                        : Colors.white.withValues(alpha: 0.2),
                                    fontFamily: 'monospace',
                                  ),
                                  textAlign: TextAlign.right,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                      AnimatedSize(
                        duration: const Duration(milliseconds: 180),
                        curve: Curves.easeInOut,
                        alignment: Alignment.topCenter,
                        child: isExpanded
                            ? Padding(
                                padding:
                                    const EdgeInsets.only(left: 38, bottom: 4),
                                child: Row(
                                  children: [
                                    SizedBox(
                                      width: 100,
                                      height: 22,
                                      child: SliderTheme(
                                        data: SliderTheme.of(context).copyWith(
                                          trackHeight: 2,
                                          thumbShape:
                                              const RoundSliderThumbShape(
                                                  enabledThumbRadius: 5),
                                          overlayShape:
                                              const RoundSliderOverlayShape(
                                                  overlayRadius: 8),
                                          activeTrackColor: Colors.white
                                              .withValues(alpha: 0.35),
                                          inactiveTrackColor: Colors.white
                                              .withValues(alpha: 0.08),
                                          thumbColor: Colors.white
                                              .withValues(alpha: 0.55),
                                          overlayColor: Colors.white
                                              .withValues(alpha: 0.04),
                                        ),
                                        child: Slider(
                                          value: _qs[i],
                                          min: 0.1,
                                          max: 10.0,
                                          onChanged: (v) => setState(() {
                                            _qs[i] = double.parse(
                                                v.toStringAsFixed(2));
                                            _updateBand(i);
                                          }),
                                        ),
                                      ),
                                    ),
                                    SizedBox(
                                      width: 44,
                                      child: Text(
                                        'Q ${_qs[i].toStringAsFixed(1)}',
                                        style: TextStyle(
                                          fontSize: 9,
                                          color: Colors.white
                                              .withValues(alpha: 0.45),
                                          fontFamily: 'monospace',
                                        ),
                                      ),
                                    ),
                                    const Spacer(),
                                    SizedBox(
                                      width: 24,
                                      height: 18,
                                      child: PopupMenuButton<int>(
                                        initialValue: _types[i],
                                        padding: EdgeInsets.zero,
                                        tooltip: _typeNames[_types[i]],
                                        onSelected: (t) => setState(() {
                                          _types[i] = t;
                                          _updateBand(i);
                                        }),
                                        itemBuilder: (_) =>
                                            List.generate(5, (ti) {
                                          return PopupMenuItem<int>(
                                            value: _filterTypes[ti],
                                            height: 28,
                                            child: Text(_typeNames[ti],
                                                style: const TextStyle(
                                                    fontSize: 11)),
                                          );
                                        }),
                                        child: Container(
                                          alignment: Alignment.center,
                                          padding: const EdgeInsets.symmetric(
                                              horizontal: 4),
                                          decoration: BoxDecoration(
                                            border: Border.all(
                                                color: Colors.white
                                                    .withValues(alpha: 0.12)),
                                            borderRadius:
                                                BorderRadius.circular(3),
                                          ),
                                          child: Text(
                                            _typeLabels[_types[i]],
                                            style: TextStyle(
                                              fontSize: 9,
                                              fontFamily: 'monospace',
                                              color: Colors.white
                                                  .withValues(alpha: 0.55),
                                            ),
                                          ),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(width: 4),
                                    SizedBox(
                                      width: 18,
                                      height: 18,
                                      child: IconButton(
                                        padding: EdgeInsets.zero,
                                        icon: Icon(
                                          _enabled[i]
                                              ? Icons.visibility
                                              : Icons.visibility_off,
                                          size: 14,
                                          color: _enabled[i]
                                              ? Colors.white
                                                  .withValues(alpha: 0.5)
                                              : Colors.white
                                                  .withValues(alpha: 0.2),
                                        ),
                                        onPressed: () => setState(() {
                                          _enabled[i] = !_enabled[i];
                                          AudioEngine.setEqBandEnabled(
                                              i, _enabled[i]);
                                        }),
                                      ),
                                    ),
                                  ],
                                ),
                              )
                            : const SizedBox.shrink(),
                      ),
                    ],
                  );
                }),
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context, rootNavigator: true).pop(),
          child: const Text('Close', style: TextStyle(fontSize: 13)),
        ),
      ],
    );
  }
}

class _EqCurvePainter extends CustomPainter {
  _EqCurvePainter(this.points, this.bands, this.enabled, this.gains);

  final List<Offset> points;
  final List<({double freq, String label})> bands;
  final List<bool> enabled;
  final List<double> gains;

  @override
  void paint(Canvas canvas, Size size) {
    const padL = 44.0, padR = 12.0, padT = 16.0, padB = 28.0;

    final gridPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.07)
      ..strokeWidth = 0.5;
    final zeroPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.2)
      ..strokeWidth = 0.7;
    final labelPaint = TextStyle(
      color: Colors.white.withValues(alpha: 0.25),
      fontSize: 8,
      fontFamily: 'monospace',
    );
    final freqLabelPaint = TextStyle(
      color: Colors.white.withValues(alpha: 0.2),
      fontSize: 7,
      fontFamily: 'monospace',
    );

    // Grid: horizontal lines at -12, -6, 0, +6, +12 dB
    for (int db = -12; db <= 12; db += 6) {
      final yNorm = (db + 12) / 24;
      final y = size.height - padB - yNorm * (size.height - padT - padB);
      final isZero = db == 0;
      canvas.drawLine(
        Offset(padL, y),
        Offset(size.width - padR, y),
        isZero ? zeroPaint : gridPaint,
      );
      if (!isZero) {
        canvas.drawRect(
          Rect.fromLTWH(2, y - 5, padL - 6, 10),
          Paint()..color = Colors.white.withValues(alpha: 0.0001),
        );
      }
    }

    // Y-axis labels
    final labelBuilder = TextPainter(textDirection: TextDirection.ltr);
    for (int db = -12; db <= 12; db += 6) {
      if (db == 0) continue;
      final yNorm = (db + 12) / 24;
      final y = size.height - padB - yNorm * (size.height - padT - padB);
      labelBuilder.text = TextSpan(text: '$db', style: labelPaint);
      labelBuilder.layout();
      labelBuilder.paint(canvas, Offset(padL - 6 - labelBuilder.width, y - 4));
    }

    // 0 dB label
    final zeroY = size.height - padB - 0.5 * (size.height - padT - padB);
    labelBuilder.text = TextSpan(text: '0', style: labelPaint);
    labelBuilder.layout();
    labelBuilder.paint(
        canvas, Offset(padL - 6 - labelBuilder.width, zeroY - 4));

    // Frequency axis labels
    for (final b in bands) {
      final t = (math.log(b.freq / 20) / math.log(1000));
      final x = padL + t * (size.width - padL - padR);
      labelBuilder.text = TextSpan(text: b.label, style: freqLabelPaint);
      labelBuilder.layout();
      labelBuilder.paint(
          canvas, Offset(x - labelBuilder.width / 2, size.height - padB + 6));
    }

    // Frequency markers (vertical dotted lines)
    final markerPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.06)
      ..strokeWidth = 0.5;
    for (int i = 0; i < bands.length; i++) {
      if (!enabled[i]) continue;
      final t = (math.log(bands[i].freq / 20) / math.log(1000));
      final x = padL + t * (size.width - padL - padR);
      canvas.drawLine(
        Offset(x, padT),
        Offset(x, size.height - padB),
        markerPaint,
      );
    }

    if (points.isEmpty) return;

    // Fill below curve
    final fillPath = Path()..moveTo(points[0].dx, size.height - padB);
    for (final p in points) {
      fillPath.lineTo(p.dx, p.dy);
    }
    fillPath.lineTo(points.last.dx, size.height - padB);
    fillPath.close();

    final fillPaint = Paint()
      ..shader = LinearGradient(
        begin: Alignment.topCenter,
        end: Alignment.bottomCenter,
        colors: [
          const Color(0xFF4CAF50).withValues(alpha: 0.15),
          const Color(0xFF4CAF50).withValues(alpha: 0.02),
        ],
      ).createShader(Rect.fromLTWH(0, 0, size.width, size.height));
    canvas.drawPath(fillPath, fillPaint);

    // Curve line
    final curvePaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.7)
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;

    final curvePath = Path()..moveTo(points[0].dx, points[0].dy);
    for (int i = 1; i < points.length; i++) {
      curvePath.lineTo(points[i].dx, points[i].dy);
    }
    canvas.drawPath(curvePath, curvePaint);

    // Band dots
    final dotPaint = Paint()
      ..color = const Color(0xFF4CAF50).withValues(alpha: 0.6)
      ..style = PaintingStyle.fill;
    for (int i = 0; i < bands.length; i++) {
      if (!enabled[i]) continue;
      final t = (math.log(bands[i].freq / 20) / math.log(1000));
      final x = padL + t * (size.width - padL - padR);
      final idx =
          (t * (_numCurvePoints - 1)).round().clamp(0, _numCurvePoints - 1);
      final y = points[idx].dy;
      canvas.drawCircle(Offset(x, y), 2.5, dotPaint);
    }
  }

  @override
  bool shouldRepaint(_EqCurvePainter old) => true;
}
