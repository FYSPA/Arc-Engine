import 'package:flutter/material.dart';

class StatusDisplay extends StatefulWidget {
  final String status;

  const StatusDisplay({super.key, required this.status});

  @override
  State<StatusDisplay> createState() => _StatusDisplayState();
}

class _StatusDisplayState extends State<StatusDisplay>
    with TickerProviderStateMixin {
  late AnimationController _fadeController;
  late Animation<double> _fadeAnimation;

  String _currentStatus = '';

  @override
  void initState() {
    super.initState();
    _currentStatus = widget.status;

    _fadeController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 400),
    );

    _fadeAnimation = CurvedAnimation(
      parent: _fadeController,
      curve: Curves.easeInOut,
    );

    _fadeController.value = 1.0;
  }

  @override
  void didUpdateWidget(StatusDisplay oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.status != oldWidget.status) {
      _currentStatus = widget.status;
      _fadeController.forward(from: 0.0);
    }
  }

  @override
  void dispose() {
    _fadeController.dispose();
    super.dispose();
  }

  StatusType _getStatusType(String status) {
    if (status.contains('✅')) return StatusType.success;
    if (status.contains('❌')) return StatusType.error;
    if (status.contains('▶️') || status.contains('🔊')) return StatusType.playing;
    return StatusType.idle;
  }

  IconData _getIcon(StatusType type) {
    switch (type) {
      case StatusType.success:
        return Icons.check_circle_rounded;
      case StatusType.error:
        return Icons.error_rounded;
      case StatusType.playing:
        return Icons.play_circle_rounded;
      case StatusType.idle:
        return Icons.info_rounded;
    }
  }

  Color _getColor(StatusType type) {
    switch (type) {
      case StatusType.success:
        return const Color(0xFF4CAF50);
      case StatusType.error:
        return const Color(0xFFEF5350);
      case StatusType.playing:
        return const Color(0xFF7C4DFF);
      case StatusType.idle:
        return const Color(0xFFB0B0B0);
    }
  }

  String _cleanStatus(String status) {
    return status.replaceAll(RegExp(r'[✅❌▶️🔊]'), '').trim();
  }

  @override
  Widget build(BuildContext context) {
    final type = _getStatusType(_currentStatus);
    final cleanedText = _cleanStatus(_currentStatus);

    return Card(
      child: FadeTransition(
        opacity: _fadeAnimation,
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Row(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: _getColor(type).withValues(alpha: 0.12),
                ),
                child: Icon(
                  _getIcon(type),
                  color: _getColor(type),
                  size: 24,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      _getStatusLabel(type),
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: _getColor(type),
                        letterSpacing: 0.5,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      cleanedText.isEmpty ? 'Waiting...' : cleanedText,
                      style: TextStyle(
                        fontSize: 14,
                        color: Colors.white.withValues(alpha: 0.8),
                        fontWeight:
                            cleanedText.isEmpty ? FontWeight.normal : FontWeight.w500,
                      ),
                    ),
                  ],
                ),
              ),
              if (type == StatusType.playing)
                _PlayingIndicator(color: _getColor(type)),
            ],
          ),
        ),
      ),
    );
  }

  String _getStatusLabel(StatusType type) {
    switch (type) {
      case StatusType.success:
        return 'SUCCESS';
      case StatusType.error:
        return 'ERROR';
      case StatusType.playing:
        return 'NOW PLAYING';
      case StatusType.idle:
        return 'READY';
    }
  }
}

enum StatusType { success, error, playing, idle }

class _PlayingIndicator extends StatefulWidget {
  final Color color;

  const _PlayingIndicator({required this.color});

  @override
  State<_PlayingIndicator> createState() => _PlayingIndicatorState();
}

class _PlayingIndicatorState extends State<_PlayingIndicator>
    with SingleTickerProviderStateMixin {
  late AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 800),
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 32,
      height: 24,
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
        children: List.generate(3, (i) {
          return AnimatedBuilder(
            animation: _controller,
            builder: (context, _) {
              final delay = i * 0.15;
              final value = ((_controller.value - delay) % 1.0).clamp(0.0, 1.0);
              final height = 4.0 + (value * 16.0);

              return Container(
                width: 4,
                height: height,
                decoration: BoxDecoration(
                  color: widget.color.withValues(alpha: 0.7),
                  borderRadius: BorderRadius.circular(2),
                ),
              );
            },
          );
        }),
      ),
    );
  }
}
