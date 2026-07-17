import 'package:flutter/material.dart';

class StatusDisplay extends StatelessWidget {
  final String status;

  const StatusDisplay({super.key, required this.status});

  @override
  Widget build(BuildContext context) {
    final isSuccess = status.contains('loaded') || status.contains('Imported');
    final isError = RegExp(
            r'(Error|failed|not found|start error|HTTP \d|empty|Cannot read)',
            caseSensitive: false)
        .hasMatch(status);
    final isPlaying = status.contains('Playing') ||
        status.contains('Streaming') ||
        status == 'Resumed';

    Color iconColor;
    IconData icon;
    if (isSuccess) {
      iconColor = const Color(0xFF4CAF50);
      icon = Icons.check_circle_rounded;
    } else if (isError) {
      iconColor = const Color(0xFFEF5350);
      icon = Icons.error_rounded;
    } else if (isPlaying) {
      iconColor = Theme.of(context).colorScheme.primary;
      icon = Icons.play_circle_rounded;
    } else {
      iconColor = Colors.white.withValues(alpha: 0.3);
      icon = Icons.circle_rounded;
    }

    final cleaned = status.trim();

    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Row(
          children: [
            Icon(icon, color: iconColor, size: 18),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                cleaned.isEmpty ? 'Ready' : cleaned,
                style: TextStyle(
                  fontSize: 13,
                  color: Colors.white.withValues(alpha: 0.7),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
