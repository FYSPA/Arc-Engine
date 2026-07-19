// ---------------------------------------------------------------------------
// File: audio_controls.dart
// Purpose: Row of format-specific playback buttons (FLAC, WAV, MP3, AAC,
//          OGG, Stream) for the example app.
// Importance: Provides quick-test UI for each audio format.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

import 'package:flutter/material.dart';

class AudioControls extends StatelessWidget {
  final VoidCallback? onPlayFlac;
  final VoidCallback? onPlayWav;
  final VoidCallback? onPlayMp3;
  final VoidCallback? onPlayAac;
  final VoidCallback? onPlayOgg;
  final VoidCallback? onStream;
  final bool isPlaying;

  const AudioControls({
    super.key,
    required this.onPlayFlac,
    required this.onPlayWav,
    this.onPlayMp3,
    this.onPlayAac,
    this.onPlayOgg,
    this.onStream,
    this.isPlaying = false,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Playback',
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: Colors.white.withValues(alpha: 0.7),
              ),
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                _formatButton(
                    context, 'FLAC', Icons.music_note_rounded, onPlayFlac),
                _formatButton(context, 'WAV', Icons.waves_rounded, onPlayWav),
                if (onPlayMp3 != null)
                  _formatButton(
                      context, 'MP3', Icons.audio_file_rounded, onPlayMp3),
                if (onPlayAac != null)
                  _formatButton(
                      context, 'AAC', Icons.audio_file_rounded, onPlayAac),
                if (onPlayOgg != null)
                  _formatButton(
                      context, 'OGG', Icons.audio_file_rounded, onPlayOgg),
                if (onStream != null)
                  _formatButton(context, 'Stream', Icons.cloud_download_rounded,
                      onStream),
              ],
            ),
            if (isPlaying) ...[
              const SizedBox(height: 12),
              Text(
                'Playing...',
                style: TextStyle(
                  fontSize: 12,
                  color: Theme.of(context).colorScheme.primary,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _formatButton(
    BuildContext context,
    String label,
    IconData icon,
    VoidCallback? onPressed,
  ) {
    return OutlinedButton.icon(
      onPressed: onPressed,
      icon: Icon(icon, size: 16),
      label: Text(label, style: const TextStyle(fontSize: 13)),
      style: OutlinedButton.styleFrom(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
        side: BorderSide(
          color: Colors.white.withValues(alpha: 0.1),
        ),
      ),
    );
  }
}
