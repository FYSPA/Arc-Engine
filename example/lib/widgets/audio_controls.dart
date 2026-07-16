import 'package:flutter/material.dart';

class AudioControls extends StatelessWidget {
  final VoidCallback? onPlayFlac;
  final VoidCallback? onPlayWav;
  final VoidCallback? onPlayMp3;
  final VoidCallback? onPlayAac;
  final VoidCallback? onPlayOgg;
  final bool isPlaying;

  const AudioControls({
    super.key,
    required this.onPlayFlac,
    required this.onPlayWav,
    this.onPlayMp3,
    this.onPlayAac,
    this.onPlayOgg,
    this.isPlaying = false,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              children: [
                Icon(
                  Icons.audiotrack_rounded,
                  size: 18,
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 8),
                Text(
                  'Playback',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: Colors.white.withValues(alpha: 0.8),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
            Row(
              children: [
                Expanded(
                  child: _AudioButton(
                    label: 'FLAC',
                    icon: Icons.music_note_rounded,
                    gradientColors: const [
                      Color(0xFF7C4DFF),
                      Color(0xFF651FFF),
                    ],
                    onPressed: onPlayFlac,
                    isPlaying: isPlaying,
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: _AudioButton(
                    label: 'WAV',
                    icon: Icons.waves_rounded,
                    gradientColors: const [
                      Color(0xFF00BCD4),
                      Color(0xFF0097A7),
                    ],
                    onPressed: onPlayWav,
                    isPlaying: isPlaying,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _AudioButton(
                    label: 'MP3',
                    icon: Icons.audio_file_rounded,
                    gradientColors: const [
                      Color(0xFFFF6F00),
                      Color(0xFFE65100),
                    ],
                    onPressed: onPlayMp3,
                    isPlaying: isPlaying,
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: _AudioButton(
                    label: 'AAC',
                    icon: Icons.audio_file_rounded,
                    gradientColors: const [
                      Color(0xFF00C853),
                      Color(0xFF009624),
                    ],
                    onPressed: onPlayAac,
                    isPlaying: isPlaying,
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: _AudioButton(
                    label: 'OGG',
                    icon: Icons.audio_file_rounded,
                    gradientColors: const [
                      Color(0xFFFF1744),
                      Color(0xFFD50000),
                    ],
                    onPressed: onPlayOgg,
                    isPlaying: isPlaying,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Center(
              child: Text(
                isPlaying ? '🔊 Playing...' : 'Tap to play audio',
                style: TextStyle(
                  fontSize: 12,
                  color: Colors.white.withValues(alpha: 0.4),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _AudioButton extends StatefulWidget {
  final String label;
  final IconData icon;
  final List<Color> gradientColors;
  final VoidCallback? onPressed;
  final bool isPlaying;

  const _AudioButton({
    required this.label,
    required this.icon,
    required this.gradientColors,
    required this.onPressed,
    required this.isPlaying,
  });

  @override
  State<_AudioButton> createState() => _AudioButtonState();
}

class _AudioButtonState extends State<_AudioButton>
    with SingleTickerProviderStateMixin {
  late AnimationController _pulseController;

  bool get _isThisPlaying => widget.isPlaying;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    );
  }

  @override
  void didUpdateWidget(_AudioButton oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (_isThisPlaying) {
      _pulseController.repeat(reverse: true);
    } else {
      _pulseController.stop();
      _pulseController.reset();
    }
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _pulseController,
      builder: (context, _) {
        final scale =
            _isThisPlaying ? 1.0 - (_pulseController.value * 0.03) : 1.0;

        return Transform.scale(
          scale: scale,
          child: Container(
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(16),
              gradient: LinearGradient(
                colors: widget.gradientColors,
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
              ),
              boxShadow: [
                BoxShadow(
                  color: widget.gradientColors.first.withValues(alpha: 0.4),
                  blurRadius: 12,
                  offset: const Offset(0, 4),
                ),
              ],
            ),
            child: Material(
              color: Colors.transparent,
              child: InkWell(
                onTap: widget.onPressed,
                borderRadius: BorderRadius.circular(16),
                splashFactory: InkRipple.splashFactory,
                child: Padding(
                  padding: const EdgeInsets.symmetric(vertical: 20),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        widget.icon,
                        color: Colors.white,
                        size: 28,
                      ),
                      const SizedBox(height: 8),
                      Text(
                        widget.label,
                        style: const TextStyle(
                          color: Colors.white,
                          fontWeight: FontWeight.bold,
                          fontSize: 16,
                          letterSpacing: 0.5,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}
