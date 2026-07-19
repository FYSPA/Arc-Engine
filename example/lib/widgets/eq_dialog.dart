import 'package:flutter/material.dart';
import 'package:arc_engine/arc_engine.dart';

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
                          width: 40,
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
                        SizedBox(
                          width: 28,
                          height: 20,
                          child: PopupMenuButton<int>(
                            initialValue: _types[i],
                            padding: EdgeInsets.zero,
                            tooltip: _typeNames[_types[i]],
                            onSelected: (t) => setState(() {
                              _types[i] = t;
                              _updateBand(i);
                            }),
                            itemBuilder: (_) => List.generate(5, (ti) {
                              return PopupMenuItem<int>(
                                value: _filterTypes[ti],
                                height: 28,
                                child: Text(_typeLabels[ti],
                                    style: const TextStyle(fontSize: 11)),
                              );
                            }),
                            child: Container(
                              alignment: Alignment.center,
                              decoration: BoxDecoration(
                                color: _enabled[i]
                                    ? Colors.white.withValues(alpha: 0.1)
                                    : Colors.white.withValues(alpha: 0.04),
                                borderRadius: BorderRadius.circular(3),
                              ),
                              child: Text(
                                _typeLabels[_types[i]],
                                style: TextStyle(
                                  fontSize: 9,
                                  fontFamily: 'monospace',
                                  color: _enabled[i]
                                      ? Colors.white.withValues(alpha: 0.7)
                                      : Colors.white.withValues(alpha: 0.25),
                                ),
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
                                onChanged: (v) => setState(() {
                                  _gains[i] = v;
                                  _updateBand(i);
                                }),
                              ),
                            ),
                          ),
                        ),
                        const SizedBox(width: 4),
                        SizedBox(
                          width: 64,
                          height: 24,
                          child: SliderTheme(
                            data: SliderTheme.of(context).copyWith(
                              trackHeight: 2,
                              thumbShape: const RoundSliderThumbShape(
                                  enabledThumbRadius: 5),
                              overlayShape: const RoundSliderOverlayShape(
                                  overlayRadius: 8),
                              activeTrackColor:
                                  Colors.white.withValues(alpha: 0.4),
                              inactiveTrackColor:
                                  Colors.white.withValues(alpha: 0.08),
                              thumbColor: Colors.white.withValues(alpha: 0.6),
                              overlayColor:
                                  Colors.white.withValues(alpha: 0.04),
                            ),
                            child: Slider(
                              value: _qs[i],
                              min: 0.1,
                              max: 10.0,
                              onChanged: (v) => setState(() {
                                _qs[i] = double.parse(v.toStringAsFixed(2));
                                _updateBand(i);
                              }),
                            ),
                          ),
                        ),
                        SizedBox(
                          width: 44,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.end,
                            children: [
                              Text(
                                '${_gains[i].toStringAsFixed(0)} dB',
                                style: TextStyle(
                                  fontSize: 8,
                                  color: _enabled[i]
                                      ? Colors.white.withValues(alpha: 0.5)
                                      : Colors.white.withValues(alpha: 0.2),
                                  fontFamily: 'monospace',
                                ),
                              ),
                              Text(
                                'Q ${_qs[i].toStringAsFixed(1)}',
                                style: TextStyle(
                                  fontSize: 7,
                                  color: _enabled[i]
                                      ? Colors.white.withValues(alpha: 0.35)
                                      : Colors.white.withValues(alpha: 0.15),
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ],
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
