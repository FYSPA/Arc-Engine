import 'dart:async';
import 'package:flutter/material.dart';
import 'package:arc_engine/arc_engine.dart';

/// Visual widget showing crossfade state between current and next track.
///
/// Displays:
/// - Current track name + progress bar (green)
/// - Crossfade zone: old track fading out, new track fading in
/// - White line at the crossfade point
/// - Slider to control crossfade duration (0-5s)
class CrossfadeVisualizer extends StatefulWidget {
  final TrackPlayer player;

  const CrossfadeVisualizer({super.key, required this.player});

  @override
  State<CrossfadeVisualizer> createState() => _CrossfadeVisualizerState();
}

class _CrossfadeVisualizerState extends State<CrossfadeVisualizer> {
  StreamSubscription<Duration>? _posSub;
  StreamSubscription<PlaybackState>? _stateSub;
  StreamSubscription<String>? _nameSub;
  int _posMs = 0;
  int _durMs = 0;
  String _currentName = '';
  String _nextName = '';
  PlaybackState _state = PlaybackState.stopped;

  // Real-time crossfade state from native
  bool _isCrossfading = false;
  int _crossfadeRemaining = 0;
  int _fadeLen = 0;
  Timer? _crossfadePollTimer;

  @override
  void initState() {
    super.initState();
    _currentName = widget.player.titleClean.isNotEmpty
        ? widget.player.titleClean
        : widget.player.currentName;
    _nextName = widget.player.nextName;
    _posMs = widget.player.position.inMilliseconds;
    _durMs = widget.player.duration.inMilliseconds;
    _state = widget.player.state;

    _posSub = widget.player.onPositionChanged.listen((d) {
      if (mounted) setState(() => _posMs = d.inMilliseconds);
    });
    _stateSub = widget.player.onStateChanged.listen((s) {
      if (mounted) setState(() => _state = s);
    });
    _nameSub = widget.player.onNameChanged.listen((name) {
      if (mounted) {
        setState(() {
          _currentName = name;
          _nextName = widget.player.nextName;
        });
      }
    });

    // Poll crossfade native state (200ms)
    _crossfadePollTimer = Timer.periodic(
        const Duration(milliseconds: 200), (_) => _pollCrossfadeState());
  }

  void _pollCrossfadeState() {
    if (!mounted) return;
    final p = widget.player;
    final newCrossfading = p.isCrossfading;
    final newRemaining = p.crossfadeRemaining;
    final newFadeLen = p.fadeLen;
    final newNextName = p.nextName;
    final newDurMs = p.duration.inMilliseconds;
    final bool needsUpdate = newCrossfading != _isCrossfading ||
        (newCrossfading &&
            (newRemaining != _crossfadeRemaining || newFadeLen != _fadeLen)) ||
        (!newCrossfading && _isCrossfading) ||
        newNextName != _nextName ||
        newDurMs != _durMs;
    if (needsUpdate) {
      setState(() {
        _isCrossfading = newCrossfading;
        _crossfadeRemaining = newRemaining;
        _fadeLen = newFadeLen;
        _nextName = newNextName;
        _durMs = newDurMs;
      });
    }
  }

  @override
  void dispose() {
    _posSub?.cancel();
    _stateSub?.cancel();
    _nameSub?.cancel();
    _crossfadePollTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final bool isPlaying = _state == PlaybackState.playing;
    final double crossfadeMs = AudioEngine.crossfadeMs;
    final double posFraction =
        _durMs > 0 ? (_posMs / _durMs).clamp(0.0, 1.0) : 0.0;

    // Crossfade point: where old ends and new begins overlapping
    final double crossfadePoint = _durMs > 0 && crossfadeMs > 0
        ? ((_durMs - crossfadeMs) / _durMs).clamp(0.0, 1.0)
        : 1.0;

    // Real-time crossfade progress (0.0 = crossfade just started, 1.0 = done)
    double crossfadeProgress = 0.0;
    if (_isCrossfading && _fadeLen > 0) {
      crossfadeProgress =
          (1.0 - _crossfadeRemaining / _fadeLen).clamp(0.0, 1.0);
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.white.withValues(alpha: 0.08)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ─── Header: track name ──────────────────────────
          Row(
            children: [
              Icon(
                isPlaying ? Icons.music_note : Icons.music_note_outlined,
                size: 14,
                color: const Color(0xFF4CAF50).withValues(alpha: 0.8),
              ),
              const SizedBox(width: 6),
              Expanded(
                child: Text(
                  _currentName.isNotEmpty ? _currentName : 'No track',
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.white.withValues(alpha: 0.7),
                    fontFamily: 'monospace',
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              if (_nextName.isNotEmpty) ...[
                Icon(Icons.arrow_forward_ios,
                    size: 10, color: Colors.white.withValues(alpha: 0.3)),
                const SizedBox(width: 4),
                Text(
                  _nextName,
                  style: TextStyle(
                    fontSize: 10,
                    color: Colors.white.withValues(alpha: 0.4),
                    fontFamily: 'monospace',
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
              ],
            ],
          ),
          const SizedBox(height: 8),

          // ─── Progress bar ───────────────────────────────
          SizedBox(
            height: 6,
            child: LayoutBuilder(
              builder: (_, constraints) {
                final totalW = constraints.maxWidth;
                return Stack(
                  children: [
                    // Background
                    Container(
                      width: totalW,
                      height: 6,
                      decoration: BoxDecoration(
                        color: Colors.white.withValues(alpha: 0.06),
                        borderRadius: BorderRadius.circular(3),
                      ),
                    ),
                    // Played portion (green)
                    Container(
                      width: totalW * posFraction,
                      height: 6,
                      decoration: BoxDecoration(
                        gradient: LinearGradient(
                          colors: [
                            const Color(0xFF4CAF50).withValues(alpha: 0.6),
                            const Color(0xFF4CAF50).withValues(alpha: 0.9),
                          ],
                        ),
                        borderRadius: BorderRadius.circular(3),
                      ),
                    ),
                    // Crossfade marker (white line)
                    if (crossfadeMs > 0 && _durMs > 0)
                      Positioned(
                        left: totalW * crossfadePoint - 1,
                        child: Container(
                          width: 2,
                          height: 6,
                          color: Colors.white.withValues(alpha: 0.8),
                        ),
                      ),
                  ],
                );
              },
            ),
          ),
          const SizedBox(height: 6),

          // ─── Crossfade zone visualization ────────────────
          if (_nextName.isNotEmpty && crossfadeMs > 0 && _durMs > 0)
            SizedBox(
              height: 32,
              child: LayoutBuilder(
                builder: (_, constraints) {
                  final totalW = constraints.maxWidth;
                  final crossX = totalW * crossfadePoint;

                  // During active crossfade, animate the overlap
                  double oldEnd = totalW;
                  double newStart = crossX;
                  if (_isCrossfading) {
                    // old fades from crossX..totalW to crossX
                    oldEnd = totalW - (totalW - crossX) * crossfadeProgress;
                    // new extends from crossX
                    newStart =
                        crossX - (totalW - crossX) * crossfadeProgress * 0.3;
                    if (newStart < 0) newStart = 0;
                  }

                  return Stack(
                    children: [
                      // Old track bar (green, full width, fades out at crossfade zone)
                      Positioned(
                        left: 0,
                        top: 0,
                        width: oldEnd,
                        height: 14,
                        child: Container(
                          decoration: BoxDecoration(
                            gradient: LinearGradient(
                              colors: [
                                const Color(0xFF4CAF50).withValues(alpha: 0.4),
                                const Color(0xFF4CAF50).withValues(
                                    alpha: _isCrossfading ? 0.1 : 0.35),
                              ],
                            ),
                            borderRadius: BorderRadius.circular(3),
                          ),
                        ),
                      ),
                      // New track bar (blue, starts at crossfade point)
                      Positioned(
                        left: newStart,
                        top: 18,
                        width: totalW - newStart,
                        height: 14,
                        child: Container(
                          decoration: BoxDecoration(
                            gradient: LinearGradient(
                              colors: [
                                const Color(0xFF2196F3).withValues(
                                    alpha: _isCrossfading ? 0.5 : 0.15),
                                const Color(0xFF2196F3).withValues(alpha: 0.4),
                              ],
                            ),
                            borderRadius: BorderRadius.circular(3),
                          ),
                        ),
                      ),
                      // White crossfade line
                      if (_isCrossfading)
                        Positioned(
                          left: crossX - 0.5,
                          top: 0,
                          width: 1.5,
                          height: 32,
                          child: Container(
                            color: Colors.white.withValues(alpha: 0.6),
                          ),
                        ),
                      // Labels
                      Positioned(
                        left: 4,
                        top: 1,
                        child: Text(
                          'old',
                          style: TextStyle(
                            fontSize: 7,
                            color: Colors.white.withValues(alpha: 0.3),
                            fontFamily: 'monospace',
                          ),
                        ),
                      ),
                      Positioned(
                        left: crossX + 4,
                        top: 19,
                        child: Text(
                          'new',
                          style: TextStyle(
                            fontSize: 7,
                            color: Colors.white.withValues(alpha: 0.3),
                            fontFamily: 'monospace',
                          ),
                        ),
                      ),
                    ],
                  );
                },
              ),
            ),

          // ─── Crossfade slider ────────────────────────────
          Row(
            children: [
              Expanded(
                child: SliderTheme(
                  data: SliderTheme.of(context).copyWith(
                    trackHeight: 2,
                    thumbShape:
                        const RoundSliderThumbShape(enabledThumbRadius: 5),
                    overlayShape:
                        const RoundSliderOverlayShape(overlayRadius: 10),
                    activeTrackColor: const Color(0xFF2196F3),
                    inactiveTrackColor: Colors.white.withValues(alpha: 0.08),
                    thumbColor: const Color(0xFF2196F3),
                    overlayColor:
                        const Color(0xFF2196F3).withValues(alpha: 0.08),
                  ),
                  child: Slider(
                    value: AudioEngine.crossfadeMs,
                    min: 0.0,
                    max: 10000.0,
                    divisions: 100,
                    onChanged: (v) =>
                        setState(() => AudioEngine.crossfadeMs = v),
                  ),
                ),
              ),
              SizedBox(
                width: 42,
                child: Text(
                  AudioEngine.crossfadeMs == 0
                      ? 'Off'
                      : '${(AudioEngine.crossfadeMs / 1000).toStringAsFixed(1)}s',
                  style: TextStyle(
                    fontSize: 10,
                    color: Colors.white.withValues(alpha: 0.5),
                    fontFamily: 'monospace',
                  ),
                  textAlign: TextAlign.right,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
