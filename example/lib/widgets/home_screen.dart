import 'dart:io';

import 'package:flutter/material.dart';
import 'package:audio_engine/audio_engine.dart';
import 'package:file_picker/file_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'library_status_card.dart';
import 'status_display.dart';
import 'pcm_visualizer.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  String _audioDir = '';
  final List<FileSystemEntity> _audioFiles = [];
  bool _dirLoading = true;

  String _status = 'Ready';
  String _libStatus = '...';
  bool _engineRunning = false;
  bool _enginePaused = false;
  bool _downloading = false;
  double _downloadProgress = 0.0;
  int _downloadedBytes = 0;
  int _downloadTotal = 0;
  bool _downloadCancelled = false;
  String _lastStreamUrl = '';
  HttpClient? _downloadClient;
  int _position = 0;
  int _duration = 0;
  double _sliderValue = 0.0;

  @override
  void initState() {
    super.initState();
    _checkLibrary();
    _startPositionPoller();
    _initDir();
    _loadStreamUrl();
  }

  Future<void> _loadStreamUrl() async {
    final prefs = await SharedPreferences.getInstance();
    _lastStreamUrl = prefs.getString('stream_url') ?? '';
  }

  Future<void> _initDir() async {
    final docs = await getApplicationDocumentsDirectory();
    _audioDir = docs.path;
    await _scanAudioFiles();
    if (mounted) setState(() => _dirLoading = false);
  }

  Future<void> _scanAudioFiles() async {
    final dir = Directory(_audioDir);
    if (!dir.existsSync()) {
      _audioFiles.clear();
      return;
    }
    const extensions = {'.flac', '.wav', '.mp3', '.aac', '.ogg', '.m4a'};
    try {
      final files = dir.listSync();
      _audioFiles
        ..clear()
        ..addAll(files.where((e) {
          if (e is! File) return false;
          final ext = e.path.split('.').last.toLowerCase();
          return extensions.contains('.$ext');
        }));
      _audioFiles.sort((a, b) => (a as File)
          .path
          .split('/')
          .last
          .toLowerCase()
          .compareTo((b as File).path.split('/').last.toLowerCase()));
    } catch (e) {
      _audioFiles.clear();
      if (mounted) {
        setState(() => _status = 'Cannot read directory: $e');
      }
    }
  }

  Future<void> _pickAudioFiles() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.audio,
      allowMultiple: true,
    );
    if (result == null || result.files.isEmpty) return;
    int copied = 0;
    for (final file in result.files) {
      final destPath = '$_audioDir/${file.name}';
      if (File(destPath).existsSync()) continue;
      try {
        if (file.path != null) {
          await File(file.path!).copy(destPath);
          copied++;
        } else if (file.bytes != null) {
          await File(destPath).writeAsBytes(file.bytes!);
          copied++;
        }
      } catch (e) {
        if (mounted) setState(() => _status = 'Error copying: $e');
      }
    }
    if (copied > 0) {
      await _scanAudioFiles();
      if (mounted) setState(() => _status = 'Imported $copied file(s)');
    }
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
        });
      } else if (_engineRunning) {
        setState(() {
          _engineRunning = false;
          _enginePaused = false;
          _sliderValue = 0.0;
        });
      }
      return true;
    });
  }

  void _checkLibrary() {
    try {
      AudioEngine.getDuration();
      if (mounted) setState(() => _libStatus = 'libaudio_engine.so loaded');
    } catch (_) {
      if (mounted) setState(() => _libStatus = 'Library check failed');
    }
  }

  void _startPlayback(String path, String label) {
    final f = File(path);
    if (!f.existsSync()) {
      setState(() => _status = 'File not found: $path');
      return;
    }
    AudioEngine.stop();
    final result = AudioEngine.startAudio(path);
    setState(() {
      if (result == 0) {
        _status = 'Playing $label...';
        _engineRunning = true;
        _enginePaused = false;
        _sliderValue = 0.0;
      } else {
        _status = '$label: start error $result';
      }
    });
  }

  static const String _defaultStreamUrl =
      'https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3';

  Future<void> _showStreamUrlDialog() async {
    final ctrl = TextEditingController(
        text: _lastStreamUrl.isNotEmpty ? _lastStreamUrl : _defaultStreamUrl);
    final url = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF1A1A24),
        title: const Text('Stream URL',
            style: TextStyle(color: Colors.white, fontSize: 15)),
        content: TextField(
          controller: ctrl,
          style: const TextStyle(color: Colors.white, fontSize: 13),
          decoration: InputDecoration(
            hintText: 'https://example.com/audio.mp3',
            hintStyle: TextStyle(
                color: Colors.white.withValues(alpha: 0.3), fontSize: 13),
            border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
            filled: true,
            fillColor: Colors.white.withValues(alpha: 0.05),
            contentPadding:
                const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          ),
          keyboardType: TextInputType.url,
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx, rootNavigator: true).pop(),
            child: const Text('Cancel', style: TextStyle(color: Colors.white54)),
          ),
          TextButton(
            onPressed: () =>
                Navigator.of(ctx, rootNavigator: true).pop(ctrl.text.trim()),
            child: const Text('Load'),
          ),
        ],
      ),
    );
    ctrl.dispose();
    if (url == null || url.isEmpty) return;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('stream_url', url);
    _lastStreamUrl = url;
    await Future.delayed(Duration.zero);
    if (!mounted) return;
    _startStreamUrl(url);
  }

  Future<void> _startStreamUrl(String url) async {
    setState(() => _status = 'Connecting...');
    final result = AudioEngine.streamUrl(url);
    if (result != 0) {
      _startDownloadFallback(url);
      return;
    }
    await Future.delayed(const Duration(milliseconds: 500));
    if (!AudioEngine.isPlaying) {
      setState(() => _status = 'Stream failed, downloading...');
      _startDownloadFallback(url);
      return;
    }
    setState(() {
      _status = 'Streaming...';
      _engineRunning = true;
      _enginePaused = false;
      _sliderValue = 0.0;
    });
  }

  void _startDownloadFallback(String url) {
    setState(() {
      _status = 'Native streaming unavailable, downloading...';
      _downloading = true;
      _downloadProgress = 0.0;
      _downloadedBytes = 0;
      _downloadTotal = 0;
      _downloadCancelled = false;
    });
    _downloadAndPlay(url, 'Stream');
  }

  Future<void> _downloadAndPlay(String url, String label) async {
    try {
      final uri = Uri.parse(url);
      final segs = uri.pathSegments;
      final ext =
          segs.isNotEmpty ? segs.last.split('.').last.toLowerCase() : 'mp3';
      final dest = '$_audioDir/stream_$label.$ext';
      await Directory(_audioDir).create(recursive: true);
      _downloadClient = HttpClient()
        ..connectionTimeout = const Duration(seconds: 30);
      final request = await _downloadClient!.getUrl(uri);
      final response =
          await request.close().timeout(const Duration(seconds: 60));
      if (response.statusCode != 200) {
        if (mounted) setState(() => _status = 'HTTP ${response.statusCode}');
        return;
      }
      _downloadTotal = response.contentLength;
      final sink = File(dest).openWrite();
      await for (final chunk in response) {
        if (_downloadCancelled) {
          await sink.close();
          await File(dest).delete();
          final client = _downloadClient;
          _downloadClient = null;
          client?.close();
          if (mounted) {
            setState(() {
              _downloading = false;
              _status = 'Download cancelled';
            });
          }
          return;
        }
        sink.add(chunk);
        if (mounted) {
          setState(() {
            _downloadedBytes += chunk.length;
            if (_downloadTotal > 0) {
              _downloadProgress = _downloadedBytes / _downloadTotal;
            }
          });
        }
      }
      await sink.flush();
      await sink.close();
      final client = _downloadClient;
      _downloadClient = null;
      client?.close();
      if (!mounted) return;
      final f = File(dest);
      if (!f.existsSync() || f.lengthSync() == 0) {
        if (mounted) setState(() => _status = 'Download produced empty file');
        return;
      }
      if (mounted) setState(() => _downloading = false);
      _startPlayback(dest, label);
    } catch (e) {
      if (mounted) setState(() => _status = 'Download: $e');
    } finally {
      if (mounted) setState(() => _downloading = false);
    }
  }

  void _cancelDownload() {
    _downloadCancelled = true;
    final client = _downloadClient;
    _downloadClient = null;
    client?.close();
  }

  void _togglePause() {
    if (_enginePaused) {
      AudioEngine.resume();
      setState(() {
        _enginePaused = false;
        _status = 'Resumed';
      });
    } else {
      AudioEngine.pause();
      setState(() {
        _enginePaused = true;
        _status = 'Paused';
      });
    }
  }

  void _stopPlayback() {
    AudioEngine.stop();
    setState(() {
      _engineRunning = false;
      _enginePaused = false;
      _sliderValue = 0.0;
      _status = 'Stopped';
    });
  }

  String _fileName(String path) => path.split('/').last;

  String _fileSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }

  String _formatTime(int ms) {
    final sec = ms ~/ 1000;
    final cs = (ms % 1000) ~/ 10;
    return '$sec:${cs.toString().padLeft(2, '0')}';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            SliverToBoxAdapter(child: _buildHeader(context)),
            SliverToBoxAdapter(
              child: Padding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 24),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    LibraryStatusCard(status: _libStatus),
                    const SizedBox(height: 16),
                    _buildFileList(context),
                    if (_engineRunning) ...[
                      const SizedBox(height: 16),
                      _buildPlayerBar(context),
                    ],
                    if (_downloading) ...[
                      const SizedBox(height: 16),
                      _buildDownloadBar(context),
                    ],
                    const SizedBox(height: 16),
                    const PcmVisualizer(),
                    const SizedBox(height: 16),
                    StatusDisplay(status: _status),
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
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 20, 20, 16),
      child: Row(
        children: [
          Text(
            'Audio Engine',
            style: TextStyle(
              fontSize: 22,
              fontWeight: FontWeight.w600,
              color: Colors.white.withValues(alpha: 0.9),
              letterSpacing: -0.5,
            ),
          ),
          const Spacer(),
          OutlinedButton.icon(
            onPressed: _showStreamUrlDialog,
            icon: const Icon(Icons.cloud_download_rounded, size: 16),
            label: const Text('Stream', style: TextStyle(fontSize: 12)),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(8),
              ),
              side: BorderSide(
                color: Colors.white.withValues(alpha: 0.12),
              ),
            ),
          ),
          const SizedBox(width: 8),
          OutlinedButton.icon(
            onPressed: _pickAudioFiles,
            icon: const Icon(Icons.add_rounded, size: 16),
            label: const Text('Files', style: TextStyle(fontSize: 12)),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(8),
              ),
              side: BorderSide(
                color: Colors.white.withValues(alpha: 0.12),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildFileList(BuildContext context) {
    if (_dirLoading) {
      return const Card(
        child: Padding(
          padding: EdgeInsets.all(24),
          child: Center(
            child: SizedBox(
              width: 20,
              height: 20,
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
          ),
        ),
      );
    }

    return Card(
      child: _audioFiles.isEmpty
          ? Padding(
              padding: const EdgeInsets.all(20),
              child: Center(
                child: Text(
                  'No audio files found\nTap + to import',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    color: Colors.white.withValues(alpha: 0.35),
                    fontSize: 13,
                    height: 1.5,
                  ),
                ),
              ),
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(16, 14, 16, 4),
                  child: Text(
                    'Files (${_audioFiles.length})',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      color: Colors.white.withValues(alpha: 0.5),
                    ),
                  ),
                ),
                ListBody(
                  children: _audioFiles.map((e) {
                    final file = e as File;
                    final path = file.path;
                    final name = _fileName(path);
                    return InkWell(
                      onTap: () => _startPlayback(path, name),
                      child: Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 16, vertical: 12),
                        child: Row(
                          children: [
                            Icon(
                              Icons.music_note_rounded,
                              size: 16,
                              color: Colors.white.withValues(alpha: 0.3),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    name,
                                    style: TextStyle(
                                      fontSize: 13,
                                      color:
                                          Colors.white.withValues(alpha: 0.8),
                                    ),
                                    overflow: TextOverflow.ellipsis,
                                  ),
                                  const SizedBox(height: 2),
                                  Text(
                                    _fileSize(file.lengthSync()),
                                    style: TextStyle(
                                      fontSize: 11,
                                      color:
                                          Colors.white.withValues(alpha: 0.35),
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            Icon(
                              Icons.play_arrow_rounded,
                              size: 18,
                              color: Colors.white.withValues(alpha: 0.3),
                            ),
                          ],
                        ),
                      ),
                    );
                  }).toList(),
                ),
              ],
            ),
    );
  }

  Widget _buildPlayerBar(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              children: [
                Text(
                  _formatTime(_position),
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.white.withValues(alpha: 0.4),
                  ),
                ),
                Expanded(
                  child: SliderTheme(
                    data: SliderTheme.of(context).copyWith(
                      trackHeight: 3,
                      thumbShape: const RoundSliderThumbShape(
                          enabledThumbRadius: 6),
                      overlayShape: const RoundSliderOverlayShape(
                          overlayRadius: 12),
                      overlayColor: Theme.of(context)
                          .colorScheme
                          .primary
                          .withValues(alpha: 0.08),
                    ),
                    child: Slider(
                      value: _sliderValue,
                      min: 0.0,
                      max: _duration > 0 ? _duration.toDouble() : 1.0,
                      onChanged: (v) => setState(() => _sliderValue = v),
                      onChangeEnd: (v) {
                        AudioEngine.seek(v.toInt());
                        setState(() => _position = v.toInt());
                      },
                    ),
                  ),
                ),
                Text(
                  _formatTime(_duration),
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.white.withValues(alpha: 0.4),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                IconButton(
                  onPressed: _togglePause,
                  icon: Icon(
                    _enginePaused
                        ? Icons.play_arrow_rounded
                        : Icons.pause_rounded,
                    size: 28,
                  ),
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: _stopPlayback,
                  icon: const Icon(Icons.stop_rounded, size: 28),
                  color: Colors.white.withValues(alpha: 0.5),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildDownloadBar(BuildContext context) {
    final pct =
        _downloadTotal > 0 ? (_downloadedBytes * 100 ~/ _downloadTotal) : 0;
    final sizeStr = _fileSize(_downloadedBytes);
    final totalStr = _downloadTotal > 0 ? _fileSize(_downloadTotal) : '?';

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              children: [
                const SizedBox(
                  width: 14,
                  height: 14,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
                const SizedBox(width: 10),
                Text(
                  'Downloading...',
                  style: TextStyle(
                    fontSize: 13,
                    color: Colors.white.withValues(alpha: 0.7),
                  ),
                ),
                const Spacer(),
                SizedBox(
                  height: 26,
                  child: TextButton(
                    onPressed: _cancelDownload,
                    style: TextButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 8),
                      minimumSize: Size.zero,
                    ),
                    child: const Text('Cancel', style: TextStyle(fontSize: 12)),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            ClipRRect(
              borderRadius: BorderRadius.circular(2),
              child: LinearProgressIndicator(
                value: _downloadProgress > 0 ? _downloadProgress : null,
                minHeight: 3,
                backgroundColor: Colors.white.withValues(alpha: 0.08),
              ),
            ),
            const SizedBox(height: 6),
            Text(
              '$sizeStr / $totalStr ($pct%)',
              style: TextStyle(
                fontSize: 11,
                color: Colors.white.withValues(alpha: 0.35),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
