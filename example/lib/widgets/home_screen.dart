import 'dart:io';

import 'package:flutter/material.dart';
import 'package:audio_engine/audio_engine.dart';

import 'library_status_card.dart';
import 'audio_controls.dart';
import 'status_display.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  static const _dir =
      '/storage/emulated/0/Android/data/com.example.audio_engine_example/files';

  String _status = 'Ready';
  String _libStatus = '...';
  bool _isPlaying = false;
  bool _engineRunning = false;
  bool _enginePaused = false;
  int _position = 0;
  int _duration = 0;
  double _sliderValue = 0.0;

  @override
  void initState() {
    super.initState();
    _checkLibrary();
    _startPositionPoller();
  }

  void _startPositionPoller() {
    Future.doWhile(() async {
      await Future.delayed(const Duration(milliseconds: 250));
      if (!mounted) return false;
      if (AudioEngine.isPlaying) {
        final pos = AudioEngine.getPosition();
        final dur = AudioEngine.getDuration();
        setState(() {
          _position = pos;
          _duration = dur;
          _sliderValue = dur > 0 ? pos.toDouble() : 0.0;
          _engineRunning = true;
          _isPlaying = true;
        });
      } else if (_engineRunning) {
        setState(() {
          _engineRunning = false;
          _enginePaused = false;
          _isPlaying = false;
          _sliderValue = 0.0;
        });
      }
      return true;
    });
  }

  void _checkLibrary() {
    // Verify library is loaded by checking if we can call startAudio
    final path = '$_dir/test.wav';
    if (File(path).existsSync()) {
      final r = AudioEngine.startAudio(path);
      if (r == 0) {
        // Started successfully - stop immediately
        AudioEngine.stop();
        if (mounted) setState(() => _libStatus = '✅ libaudio_engine.so loaded');
        return;
      }
    } else {
      // Fallback: check if FFI bindings load at all
      try {
        AudioEngine.getFlacInfo('/nonexistent/file.flac');
      } catch (_) {}
      // If we got here without crash, the library is loaded
      if (mounted) setState(() => _libStatus = '✅ libaudio_engine.so loaded');
    }
  }

  // Non-blocking: uses engine background thread
  void _startPlayback(String file, String label) {
    final path = '$_dir/$file';
    final f = File(path);
    if (!f.existsSync()) {
      setState(() => _status = '❌ File not found: $file');
      return;
    }
    AudioEngine.stop();
    final result = AudioEngine.startAudio(path);
    setState(() {
      if (result == 0) {
        _status = '🔊 Playing $label...';
        _engineRunning = true;
        _enginePaused = false;
        _isPlaying = true;
        _sliderValue = 0.0;
      } else {
        _status = '❌ $label: start error $result';
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            SliverToBoxAdapter(
              child: _buildHeader(context),
            ),
            SliverToBoxAdapter(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 20),
                child: Column(
                  children: [
                    LibraryStatusCard(status: _libStatus),
                    const SizedBox(height: 16),
                    AudioControls(
                      isPlaying: _isPlaying,
                      onPlayFlac: () => _startPlayback('test.flac', 'FLAC'),
                      onPlayWav: () => _startPlayback('test.wav', 'WAV'),
                      onPlayMp3: () => _startPlayback('test.mp3', 'MP3'),
                      onPlayAac: () => _startPlayback('test.aac', 'AAC'),
                      onPlayOgg: () => _startPlayback('test.ogg', 'OGG'),
                    ),
                    const SizedBox(height: 16),
                    _buildControls(context),
                    const SizedBox(height: 16),
                    StatusDisplay(status: _status),
                    const SizedBox(height: 32),
                    _buildFileInfo(context),
                    const SizedBox(height: 24),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildHeader(BuildContext context) {
    return Container(
      padding: EdgeInsets.only(
        top: MediaQuery.of(context).padding.top + 20,
        bottom: 24,
        left: 24,
        right: 24,
      ),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Theme.of(context).colorScheme.primary.withValues(alpha: 0.3),
            Theme.of(context).colorScheme.primary.withValues(alpha: 0.05),
            const Color(0xFF0D0D1A),
          ],
          stops: const [0.0, 0.4, 1.0],
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(14),
                  gradient: const LinearGradient(
                    colors: [Color(0xFF7C4DFF), Color(0xFF651FFF)],
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                  ),
                  boxShadow: [
                    BoxShadow(
                      color: const Color(0xFF7C4DFF).withValues(alpha: 0.4),
                      blurRadius: 12,
                      offset: const Offset(0, 4),
                    ),
                  ],
                ),
                child: const Icon(
                  Icons.music_note_rounded,
                  color: Colors.white,
                  size: 24,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Audio Engine',
                      style: TextStyle(
                        fontSize: 24,
                        fontWeight: FontWeight.bold,
                        color: Colors.white.withValues(alpha: 0.95),
                        letterSpacing: -0.5,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'Playback Controls',
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.white.withValues(alpha: 0.45),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  String _formatTime(int ms) {
    final sec = ms ~/ 1000;
    final cs = (ms % 1000) ~/ 10;
    return '$sec:${cs.toString().padLeft(2, '0')}';
  }

  Widget _buildControls(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              children: [
                Icon(Icons.settings_rounded,
                    size: 18, color: Theme.of(context).colorScheme.primary),
                const SizedBox(width: 8),
                Text('Controls',
                    style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: Colors.white.withValues(alpha: 0.8))),
              ],
            ),
            const SizedBox(height: 16),
            if (_engineRunning) ...[
              Row(
                children: [
                  Text(_formatTime(_position),
                      style: TextStyle(
                          fontSize: 11,
                          color: Colors.white.withValues(alpha: 0.4))),
                  const Spacer(),
                  Text(_formatTime(_duration),
                      style: TextStyle(
                          fontSize: 11,
                          color: Colors.white.withValues(alpha: 0.4))),
                ],
              ),
              SliderTheme(
                data: SliderTheme.of(context).copyWith(
                  trackHeight: 4,
                  thumbShape:
                      const RoundSliderThumbShape(enabledThumbRadius: 8),
                  overlayShape:
                      const RoundSliderOverlayShape(overlayRadius: 16),
                  activeTrackColor: Theme.of(context).colorScheme.primary,
                  inactiveTrackColor: Colors.white.withValues(alpha: 0.15),
                  thumbColor: Theme.of(context).colorScheme.primary,
                  overlayColor: Theme.of(context)
                      .colorScheme
                      .primary
                      .withValues(alpha: 0.12),
                ),
                child: Slider(
                  value: _sliderValue,
                  min: 0.0,
                  max: _duration > 0 ? _duration.toDouble() : 1.0,
                  onChanged: (v) {
                    setState(() => _sliderValue = v);
                  },
                  onChangeEnd: (v) {
                    AudioEngine.seek(v.toInt());
                    setState(() => _position = v.toInt());
                  },
                ),
              ),
              const SizedBox(height: 4),
            ],
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: () {
                      if (AudioEngine.isPlaying) {
                        if (_enginePaused) {
                          AudioEngine.resume();
                          setState(() {
                            _enginePaused = false;
                            _status = '▶️ Resumed';
                          });
                        } else {
                          AudioEngine.pause();
                          setState(() {
                            _enginePaused = true;
                            _status = '⏸️ Paused';
                          });
                        }
                      }
                    },
                    icon: Icon(
                        _enginePaused
                            ? Icons.play_arrow_rounded
                            : Icons.pause_rounded,
                        size: 18),
                    label: Text(_enginePaused ? 'Resume' : 'Pause'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: () {
                      if (AudioEngine.isPlaying) {
                        AudioEngine.stop();
                        setState(() {
                          _engineRunning = false;
                          _enginePaused = false;
                          _isPlaying = false;
                          _sliderValue = 0.0;
                          _status = '⏹️ Stopped';
                        });
                      }
                    },
                    icon: const Icon(Icons.stop_rounded, size: 18),
                    label: const Text('Stop'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor:
                          const Color(0xFFEF5350).withValues(alpha: 0.2),
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildFileInfo(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Row(
          children: [
            Icon(
              Icons.folder_rounded,
              size: 20,
              color: Colors.white.withValues(alpha: 0.4),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Audio Directory',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      color: Colors.white.withValues(alpha: 0.5),
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    _dir,
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.white.withValues(alpha: 0.35),
                      fontFamily: 'monospace',
                    ),
                    overflow: TextOverflow.ellipsis,
                    maxLines: 2,
                  ),
                ],
              ),
            ),
            Icon(
              Icons.arrow_forward_ios_rounded,
              size: 14,
              color: Colors.white.withValues(alpha: 0.2),
            ),
          ],
        ),
      ),
    );
  }
}
