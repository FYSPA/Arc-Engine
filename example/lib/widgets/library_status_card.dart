import 'package:flutter/material.dart';

class LibraryStatusCard extends StatefulWidget {
  final String status;

  const LibraryStatusCard({super.key, required this.status});

  @override
  State<LibraryStatusCard> createState() => _LibraryStatusCardState();
}

class _LibraryStatusCardState extends State<LibraryStatusCard>
    with TickerProviderStateMixin {
  late AnimationController _pulseController;
  late Animation<double> _pulseAnimation;

  bool get _isLoaded => widget.status.contains('✅');

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    );

    _pulseAnimation = Tween<double>(begin: 0.85, end: 1.0).animate(
      CurvedAnimation(parent: _pulseController, curve: Curves.easeInOut),
    );

    if (_isLoaded) {
      _pulseController.repeat(reverse: true);
    }
  }

  @override
  void didUpdateWidget(LibraryStatusCard oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (_isLoaded && !_pulseController.isAnimating) {
      _pulseController.repeat(reverse: true);
    } else if (!_isLoaded && _pulseController.isAnimating) {
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
    final isLoaded = _isLoaded;

    return Card(
      child: AnimatedBuilder(
        animation: _pulseAnimation,
        builder: (context, child) {
          return Transform.scale(
            scale: isLoaded ? _pulseAnimation.value : 1.0,
            child: child,
          );
        },
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 20),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 40,
                height: 40,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: isLoaded
                      ? const Color(0xFF4CAF50).withValues(alpha: 0.15)
                      : const Color(0xFFEF5350).withValues(alpha: 0.15),
                ),
                child: Icon(
                  isLoaded ? Icons.check_circle : Icons.error_outline,
                  color: isLoaded ? const Color(0xFF4CAF50) : const Color(0xFFEF5350),
                  size: 22,
                ),
              ),
              const SizedBox(width: 16),
              Flexible(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      'Native Library',
                      style: TextStyle(
                        color: Colors.white.withValues(alpha: 0.9),
                        fontWeight: FontWeight.w600,
                        fontSize: 13,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      widget.status.replaceAll(RegExp(r'[✅❌]'), '').trim(),
                      style: TextStyle(
                        color: Colors.white.withValues(alpha: 0.5),
                        fontSize: 12,
                      ),
                    ),
                  ],
                ),
              ),
              if (isLoaded)
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: const Color(0xFF4CAF50).withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: const Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.flash_on, size: 12, color: Color(0xFF4CAF50)),
                      SizedBox(width: 4),
                      Text(
                        'ACTIVE',
                        style: TextStyle(
                          fontSize: 10,
                          fontWeight: FontWeight.bold,
                          color: Color(0xFF4CAF50),
                          letterSpacing: 0.8,
                        ),
                      ),
                    ],
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
