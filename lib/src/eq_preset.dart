import 'dart:convert';

/// Represents a complete EQ preset with gains, filter types, and Q values
/// for all 10 bands (31 Hz — 16 kHz).
class EqPreset {
  final String name;
  final List<double> gains;
  final List<int> types;
  final List<double> qs;
  final bool isBuiltIn;

  const EqPreset({
    required this.name,
    required this.gains,
    this.types = const [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    this.qs = const [
      0.707,
      0.707,
      0.707,
      0.707,
      0.707,
      0.707,
      0.707,
      0.707,
      0.707,
      0.707
    ],
    this.isBuiltIn = true,
  });

  static const bandFrequencies = [
    31.0,
    62.0,
    125.0,
    250.0,
    500.0,
    1000.0,
    2000.0,
    4000.0,
    8000.0,
    16000.0,
  ];

  // ─── Built-in presets ──────────────────────────────────────────────────────

  static const flat = EqPreset(
    name: 'Flat',
    gains: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  );

  static const rock = EqPreset(
    name: 'Rock',
    gains: [5, 3, 0, -1, -2, 0, 2, 3, 4, 3],
  );

  static const pop = EqPreset(
    name: 'Pop',
    gains: [-1, 0, 1, 3, 4, 3, 1, 0, -1, -1],
  );

  static const jazz = EqPreset(
    name: 'Jazz',
    gains: [3, 3, 2, 1, 0, 0, 1, 2, 2, 1],
  );

  static const classical = EqPreset(
    name: 'Classical',
    gains: [4, 3, 1, 0, 0, 0, 0, 1, 3, 4],
  );

  static const bassBoost = EqPreset(
    name: 'Bass Boost',
    gains: [8, 6, 4, 2, 0, 0, 0, 0, 0, 0],
    types: [1, 1, 0, 0, 0, 0, 0, 0, 0, 0],
  );

  static const trebleBoost = EqPreset(
    name: 'Treble Boost',
    gains: [0, 0, 0, 0, 0, 0, 2, 4, 6, 8],
    types: [0, 0, 0, 0, 0, 0, 0, 0, 2, 2],
  );

  static const vocal = EqPreset(
    name: 'Vocal',
    gains: [-2, -1, 0, 2, 4, 4, 3, 1, 0, -1],
  );

  static const electronic = EqPreset(
    name: 'Electronic',
    gains: [6, 4, 1, 0, -2, 0, 1, 3, 5, 5],
    types: [1, 0, 0, 0, 0, 0, 0, 0, 2, 2],
    qs: [0.5, 0.707, 0.707, 0.707, 0.707, 0.707, 0.707, 0.707, 0.5, 0.5],
  );

  static const acoustic = EqPreset(
    name: 'Acoustic',
    gains: [2, 3, 3, 1, 0, 1, 2, 3, 2, 1],
  );

  /// All built-in presets in display order.
  static const List<EqPreset> builtInPresets = [
    flat,
    rock,
    pop,
    jazz,
    classical,
    bassBoost,
    trebleBoost,
    vocal,
    electronic,
    acoustic,
  ];

  // ─── JSON serialization ────────────────────────────────────────────────────

  Map<String, dynamic> toJson() => {
        'name': name,
        'gains': gains,
        'types': types,
        'qs': qs,
      };

  factory EqPreset.fromJson(Map<String, dynamic> json) => EqPreset(
        name: json['name'] as String,
        gains:
            (json['gains'] as List).map((e) => (e as num).toDouble()).toList(),
        types: (json['types'] as List<dynamic>).map((e) => e as int).toList(),
        qs: (json['qs'] as List<dynamic>)
            .map((e) => (e as num).toDouble())
            .toList(),
        isBuiltIn: false,
      );

  static List<EqPreset> listFromJson(String raw) {
    if (raw.isEmpty) return [];
    final list = jsonDecode(raw) as List;
    return list
        .map((e) => EqPreset.fromJson(e as Map<String, dynamic>))
        .toList();
  }

  static String listToJson(List<EqPreset> presets) =>
      jsonEncode(presets.map((p) => p.toJson()).toList());
}
