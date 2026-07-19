// ---------------------------------------------------------------------------
// File: eq_dialog.dart
// Purpose: 10-band equalizer dialog with per-band gain sliders (-12 to +12 dB),
//          bypass switch, and reset button. Controls the native DspProcessor.
// Importance: UI for testing the EQ feature of the engine.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

import 'package:flutter/material.dart';
import 'package:arc_engine/arc_engine.dart';

class EqDialog extends StatefulWidget {
  const EqDialog({super.key});

  @override
  State<EqDialog> createState() => _EqDialogState();
}

class _EqDialogState extends State<EqDialog> {
  bool _bypass = false;

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
  final List<bool> _enabled = List.filled(10, false);

  @override
  void dispose() {
    super.dispose();
  }

  void _updateBand(int index, double gain) {
    _gains[index] = gain;
    _enabled[index] = gain != 0.0;
    AudioEngine.setEqBand(
        index, AudioEngine.eqPeaking, _bands[index].freq, gain, 0.707);
    AudioEngine.setEqBandEnabled(index, _enabled[index]);
  }

  void _reset() {
    setState(() {
      for (int i = 0; i < 10; i++) {
        _gains[i] = 0.0;
        _enabled[i] = false;
      }
      _bypass = false;
    });
    AudioEngine.resetEq();
    AudioEngine.setEqBypass(false);
  }

  void _toggleBypass(bool v) {
    setState(() => _bypass = v);
    AudioEngine.setEqBypass(v);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: const Color(0xFF1A1A24),
      titlePadding: const EdgeInsets.fromLTRB(20, 16, 20, 0),
      contentPadding: const EdgeInsets.fromLTRB(20, 8, 20, 0),
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
              const SizedBox(height: 8),
              const Divider(height: 1, color: Colors.white12),
              const SizedBox(height: 4),
              ListBody(
                children: List.generate(10, (i) {
                  return Padding(
                    padding: const EdgeInsets.symmetric(vertical: 2),
                    child: Row(
                      children: [
                        SizedBox(
                          width: 48,
                          child: Text(
                            _bands[i].label,
                            style: TextStyle(
                              fontSize: 10,
                              color: _enabled[i]
                                  ? Colors.white.withValues(alpha: 0.7)
                                  : Colors.white.withValues(alpha: 0.25),
                              fontFamily: 'monospace',
                            ),
                          ),
                        ),
                        Expanded(
                          child: SizedBox(
                            height: 24,
                            child: SliderTheme(
                              data: SliderTheme.of(context).copyWith(
                                trackHeight: 2,
                                thumbShape: const RoundSliderThumbShape(
                                    enabledThumbRadius: 6),
                                overlayShape: const RoundSliderOverlayShape(
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
                                onChanged: (v) =>
                                    setState(() => _updateBand(i, v)),
                              ),
                            ),
                          ),
                        ),
                        SizedBox(
                          width: 28,
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
