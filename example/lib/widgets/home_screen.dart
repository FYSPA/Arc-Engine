// ---------------------------------------------------------------------------
// File: home_screen.dart
// Purpose: Main screen of the example app. Manages audio file library,
//          multi-track playback UI (4 track cards), stream URL dialog,
//          download fallback, and PCM waveform visualization.
// Importance: Primary UI for manual testing of all engine features.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:arc_engine/arc_engine.dart';
import 'package:file_picker/file_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'library_status_card.dart';
import 'status_display.dart';
import 'pcm_visualizer.dart';
import 'waveform_widget.dart';
import 'eq_dialog.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _TrackUiState {
  final TrackPlayer player;
  String label;
  bool running = false;
  bool paused = false;
  int position = 0;
  int duration = 0;
  double sliderValue = 0.0;
  double volume = 1.0;
  double pan = 0.0;
  bool mute = false;
  bool solo = false;
  bool loop = false;
  List<double> waveformSamples = [];
  StreamSubscription<List<double>>? _waveformSub;

  StreamSubscription<String>? _nameSub;
  StreamSubscription<String>? _abortSub;

  _TrackUiState(this.player, this.label);

  int get index => player.index;

  void startNameListener(
      {VoidCallback? onNameChanged,
      void Function(String abortedName)? onAborted}) {
    _nameSub?.cancel();
    _nameSub = player.onNameChanged.listen((newName) {
      debugPrint('startNameListener[${player.index}]: "$label" -> "$newName"');
      label = newName;
      onNameChanged?.call();
    });
    _abortSub?.cancel();
    _abortSub = player.onGaplessAborted.listen((abortedName) {
      debugPrint('startNameListener[${player.index}]: ABORTED "$abortedName"');
      onAborted?.call(abortedName);
    });
  }

  void startWaveformStream(VoidCallback onUpdate) {
    _waveformSub?.cancel();
    _waveformSub = null;
    waveformSamples.clear();
    player.stopPcmStream();
    final stream = player.startPcmStream();
    _waveformSub = stream.listen((samples) {
      waveformSamples.addAll(samples);
      while (waveformSamples.length > 60) {
        waveformSamples.removeAt(0);
      }
      onUpdate();
    });
  }

  void stopWaveformStream() {
    _waveformSub?.cancel();
    _waveformSub = null;
    _nameSub?.cancel();
    _nameSub = null;
    _abortSub?.cancel();
    _abortSub = null;
    waveformSamples.clear();
    player.stopPcmStream();
  }
}

class _HomeScreenState extends State<HomeScreen> {
  String _audioDir = '';
  final List<FileSystemEntity> _audioFiles = [];
  bool _dirLoading = true;

  String _status = 'Ready';
  String _libStatus = '...';
  bool _engineRunning = false;
  bool _enginePaused = false;
  bool _showAdvancedCompressor = false;
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
  final List<_TrackUiState> _tracks = [];

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
      bool anyRunning = false;
      for (final t in _tracks) {
        final p = t.player;
        if (p.state == PlaybackState.playing) {
          anyRunning = true;
          t.running = true;
          t.position = p.position.inMilliseconds;
          t.duration = p.duration.inMilliseconds;
          t.sliderValue = t.duration > 0 ? t.position.toDouble() : 0.0;
        } else if (t.running) {
          t.running = false;
          t.paused = false;
          t.sliderValue = 0.0;
        }
      }
      if (_tracks.isNotEmpty && AudioEngine.isPlaying) {
        _position = AudioEngine.getPosition();
        _duration = AudioEngine.getDuration();
        _sliderValue = _duration > 0 ? _position.toDouble() : 0.0;
        _engineRunning = true;
      } else if (_engineRunning) {
        setState(() {
          _engineRunning = false;
          _enginePaused = false;
          _sliderValue = 0.0;
        });
      }
      if (anyRunning) setState(() {});
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

  int _findFreeTrackSlot() {
    for (int i = 0; i < 4; i++) {
      if (!_tracks.any((t) => t.index == i && t.running)) return i;
    }
    return -1;
  }

  void _startPlayback(String path, String label, {int? trackIndex}) {
    final f = File(path);
    if (!f.existsSync()) {
      setState(() => _status = 'File not found: $path');
      return;
    }
    final idx = trackIndex ?? 0;
    final player = AudioEngine.instance.tracks[idx];
    player.stop();
    final result = player.play(path);
    setState(() {
      if (result == 0) {
        _status = 'Track $idx: $label';
        _engineRunning = true;
        _enginePaused = false;
        _sliderValue = 0.0;
        final existing = _tracks.indexWhere((t) => t.index == idx);
        if (existing >= 0) {
          _tracks[existing].stopWaveformStream();
          _tracks[existing].label = label;
          _tracks[existing].running = true;
          _tracks[existing].sliderValue = 0.0;
        } else {
          _tracks.add(_TrackUiState(player, label)..running = true);
        }
        // Start name listener for gap-less tracking (re-subscribes if reused)
        final wt = _tracks.firstWhere((t) => t.index == idx);
        wt.startNameListener(
          onNameChanged: () => _reQueueNext(idx),
          onAborted: (abortedName) => _onGaplessAborted(idx, abortedName),
        );
        // Start waveform stream for this track
        wt.startWaveformStream(() {
          if (mounted) setState(() {});
        });
      } else {
        _status = '$label: start error $result';
      }
    });
  }

  void _onGaplessAborted(int trackIndex, String abortedName) {
    // Find the aborted track's full path in the library
    final match = _audioFiles.where((e) => _fileName(e.path) == abortedName);
    if (match.isEmpty) {
      debugPrint('_onGaplessAborted: "$abortedName" not found in library');
      setState(
          () => _status = 'Gapless aborted: $abortedName (not in library)');
      return;
    }
    final abortedPath = match.first.path;
    debugPrint('_onGaplessAborted[$trackIndex]: playing "$abortedName" fresh');
    setState(() => _status = 'Gapless aborted → playing $abortedName fresh');
    // Play the aborted track fresh on the same slot (new AAudio stream)
    _startPlayback(abortedPath, abortedName, trackIndex: trackIndex);
    // Re-queue the next file after this one
    final curIdx = _audioFiles.indexWhere((e) => e.path == abortedPath);
    if (curIdx >= 0 && curIdx + 1 < _audioFiles.length) {
      final nextPath = _audioFiles[curIdx + 1].path;
      final nextName = _fileName(nextPath);
      AudioEngine.instance.tracks[trackIndex]
          .setNextTrack(nextPath, name: nextName);
    }
  }

  void _assignToTrack(String path, String label) {
    final slot = _findFreeTrackSlot();
    if (slot < 0) {
      setState(() => _status = 'All 4 tracks are in use');
      return;
    }
    _startPlayback(path, label, trackIndex: slot);
    // Auto-queue next file in the list for gap-less playback
    final curIdx = _audioFiles.indexWhere((e) => e.path == path);
    if (curIdx >= 0 && curIdx + 1 < _audioFiles.length) {
      final nextPath = _audioFiles[curIdx + 1].path;
      final nextName = _fileName(nextPath);
      AudioEngine.instance.tracks[slot].setNextTrack(nextPath, name: nextName);
      setState(() => _status = '$label → $nextName queued');
    }
  }

  void _reQueueNext(int trackIndex) {
    final t = _tracks.where((t) => t.index == trackIndex && t.running);
    if (t.isEmpty) {
      debugPrint('_reQueueNext[$trackIndex]: t.isEmpty ($_tracks)');
      return;
    }
    final label = t.first.label;
    final idx = _audioFiles.indexWhere((e) => _fileName(e.path) == label);
    debugPrint(
        '_reQueueNext[$trackIndex]: label="$label" idx=$idx audioFiles=${_audioFiles.length}');
    if (idx < 0 || idx + 1 >= _audioFiles.length) {
      debugPrint(
          '_reQueueNext[$trackIndex]: no next file (idx=$idx, len=${_audioFiles.length})');
      return;
    }
    final nextPath = _audioFiles[idx + 1].path;
    final nextName = _fileName(nextPath);
    debugPrint('_reQueueNext[$trackIndex]: queuing "$nextName"');
    AudioEngine.instance.tracks[trackIndex]
        .setNextTrack(nextPath, name: nextName);
  }

  Future<void> _pickAndPlayTrack() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.audio,
      allowMultiple: false,
    );
    if (result == null || result.files.isEmpty) return;
    final file = result.files.first;
    if (file.path == null) return;
    final destPath = '$_audioDir/${file.name}';
    try {
      if (!File(destPath).existsSync()) {
        await File(file.path!).copy(destPath);
      }
      await _scanAudioFiles();
      _assignToTrack(destPath, file.name);
    } catch (e) {
      if (mounted) setState(() => _status = 'Error: $e');
    }
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
            child:
                const Text('Cancel', style: TextStyle(color: Colors.white54)),
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
                    if (_tracks.isNotEmpty) ...[
                      const SizedBox(height: 16),
                      _buildTrackList(context),
                    ],
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
          const SizedBox(width: 8),
          OutlinedButton.icon(
            onPressed: _pickAndPlayTrack,
            icon: const Icon(Icons.queue_music_rounded, size: 16),
            label: const Text('+Track', style: TextStyle(fontSize: 12)),
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
            onPressed: () => showDialog(
              context: context,
              builder: (_) => const EqDialog(),
            ),
            icon: const Icon(Icons.tune_rounded, size: 16),
            label: const Text('EQ', style: TextStyle(fontSize: 12)),
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
                    String sizeStr;
                    try {
                      sizeStr = _fileSize(file.lengthSync());
                    } catch (_) {
                      sizeStr = '?';
                    }
                    if (!File(path).existsSync()) {
                      return const SizedBox.shrink();
                    }
                    return InkWell(
                      onTap: () => _assignToTrack(path, name),
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
                                    sizeStr,
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

  Widget _buildTrackList(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 8),
          child: Text(
            'Tracks (${_tracks.length})',
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: Colors.white.withValues(alpha: 0.5),
            ),
          ),
        ),
        ..._tracks.map((t) => _buildTrackCard(context, t)),
      ],
    );
  }

  Widget _buildTrackCard(BuildContext context, _TrackUiState t) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                    decoration: BoxDecoration(
                      color: t.running
                          ? const Color(0xFF4CAF50).withValues(alpha: 0.2)
                          : Colors.white.withValues(alpha: 0.05),
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Text(
                      'Track ${t.index}',
                      style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.w700,
                        color: t.running
                            ? const Color(0xFF4CAF50)
                            : Colors.white.withValues(alpha: 0.4),
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      t.label,
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.white.withValues(alpha: 0.8),
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ],
              ),
              if (t.running) ...[
                const SizedBox(height: 8),
                Row(
                  children: [
                    Text(
                      _formatTime(t.position),
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
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 12),
                        ),
                        child: Slider(
                          value: t.sliderValue,
                          min: 0.0,
                          max: t.duration > 0 ? t.duration.toDouble() : 1.0,
                          onChanged: (v) => setState(() => t.sliderValue = v),
                          onChangeEnd: (v) {
                            t.player.seek(Duration(milliseconds: v.toInt()));
                            setState(() => t.position = v.toInt());
                          },
                        ),
                      ),
                    ),
                    Text(
                      _formatTime(t.duration),
                      style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.4),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                WaveformWidget(
                  samples: t.waveformSamples,
                  color: t.index == 0
                      ? const Color(0xFF7C4DFF)
                      : t.index == 1
                          ? const Color(0xFF4CAF50)
                          : t.index == 2
                              ? const Color(0xFFFFA726)
                              : const Color(0xFFEF5350),
                  height: 40,
                ),
                Row(
                  children: [
                    IconButton(
                      onPressed: () {
                        if (t.paused) {
                          t.player.resume();
                        } else {
                          t.player.pause();
                        }
                        setState(() => t.paused = !t.paused);
                      },
                      icon: Icon(
                        t.paused
                            ? Icons.play_arrow_rounded
                            : Icons.pause_rounded,
                        size: 22,
                      ),
                      color: Colors.white.withValues(alpha: 0.6),
                      constraints:
                          const BoxConstraints(minWidth: 36, minHeight: 36),
                      padding: EdgeInsets.zero,
                    ),
                    IconButton(
                      onPressed: () {
                        t.stopWaveformStream();
                        t.player.stop();
                        setState(() {
                          t.running = false;
                          t.paused = false;
                          t.sliderValue = 0.0;
                          _tracks.removeWhere(
                              (x) => x.index == t.index && !x.running);
                        });
                      },
                      icon: const Icon(Icons.stop_rounded, size: 22),
                      color: Colors.white.withValues(alpha: 0.4),
                      constraints:
                          const BoxConstraints(minWidth: 36, minHeight: 36),
                      padding: EdgeInsets.zero,
                    ),
                    // Mute button
                    IconButton(
                      onPressed: () => setState(() {
                        t.mute = !t.mute;
                        t.player.mute = t.mute;
                      }),
                      icon: const Text('M',
                          style: TextStyle(
                              fontSize: 11, fontWeight: FontWeight.w700)),
                      color: t.mute
                          ? const Color(0xFFEF5350)
                          : Colors.white.withValues(alpha: 0.35),
                      constraints:
                          const BoxConstraints(minWidth: 32, minHeight: 32),
                      padding: EdgeInsets.zero,
                      tooltip: 'Mute',
                    ),
                    // Solo button
                    IconButton(
                      onPressed: () => setState(() {
                        t.solo = !t.solo;
                        t.player.solo = t.solo;
                      }),
                      icon: const Text('S',
                          style: TextStyle(
                              fontSize: 11, fontWeight: FontWeight.w700)),
                      color: t.solo
                          ? const Color(0xFFFFA726)
                          : Colors.white.withValues(alpha: 0.35),
                      constraints:
                          const BoxConstraints(minWidth: 32, minHeight: 32),
                      padding: EdgeInsets.zero,
                      tooltip: 'Solo',
                    ),
                    // Loop button
                    IconButton(
                      onPressed: () => setState(() {
                        t.loop = !t.loop;
                        t.player.loop = t.loop;
                      }),
                      icon: const Text('L',
                          style: TextStyle(
                              fontSize: 11, fontWeight: FontWeight.w700)),
                      color: t.loop
                          ? const Color(0xFF42A5F5)
                          : Colors.white.withValues(alpha: 0.35),
                      constraints:
                          const BoxConstraints(minWidth: 32, minHeight: 32),
                      padding: EdgeInsets.zero,
                      tooltip: 'Loop',
                    ),
                    const Spacer(),
                    SizedBox(
                      width: 80,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                        ),
                        child: Slider(
                          value: t.volume,
                          min: 0.0,
                          max: 1.0,
                          divisions: 20,
                          onChanged: (v) {
                            t.player.volume = v;
                            setState(() => t.volume = v);
                          },
                        ),
                      ),
                    ),
                    SizedBox(
                      width: 80,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                        ),
                        child: Slider(
                          value: t.pan,
                          min: -1.0,
                          max: 1.0,
                          divisions: 20,
                          onChanged: (v) {
                            t.player.pan = v;
                            setState(() => t.pan = v);
                          },
                        ),
                      ),
                    ),
                  ],
                ),
              ],
            ],
          ),
        ),
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
                      thumbShape:
                          const RoundSliderThumbShape(enabledThumbRadius: 6),
                      overlayShape:
                          const RoundSliderOverlayShape(overlayRadius: 12),
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
            const SizedBox(height: 6),
            const Divider(height: 1, color: Colors.white12),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Limiter',
                    style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.5))),
                const SizedBox(width: 6),
                SizedBox(
                  height: 22,
                  child: Switch.adaptive(
                    value: AudioEngine.limiterEnabled,
                    onChanged: (v) =>
                        setState(() => AudioEngine.limiterEnabled = v),
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  ),
                ),
                if (AudioEngine.limiterEnabled) ...[
                  const SizedBox(width: 4),
                  Text('Thresh',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.limiterThreshold,
                          min: -60.0,
                          max: 0.0,
                          divisions: 60,
                          onChanged: (v) =>
                              setState(() => AudioEngine.limiterThreshold = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 36,
                    child: Text(
                      '${AudioEngine.limiterThreshold.toStringAsFixed(1)} dB',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                ],
              ],
            ),
            const SizedBox(height: 6),
            const Divider(height: 1, color: Colors.white12),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Compressor',
                    style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.5))),
                const SizedBox(width: 6),
                SizedBox(
                  height: 22,
                  child: Switch.adaptive(
                    value: AudioEngine.compressorEnabled,
                    onChanged: (v) {
                      setState(() => AudioEngine.compressorEnabled = v);
                      if (!v) _showAdvancedCompressor = false;
                    },
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  ),
                ),
                if (AudioEngine.compressorEnabled) ...[
                  const SizedBox(width: 4),
                  Text('Thresh',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorThreshold,
                          min: -60.0,
                          max: 0.0,
                          divisions: 60,
                          onChanged: (v) => setState(
                              () => AudioEngine.compressorThreshold = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 36,
                    child: Text(
                      '${AudioEngine.compressorThreshold.toStringAsFixed(1)} dB',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 4),
                  Text('Ratio',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorRatio,
                          min: 1.0,
                          max: 20.0,
                          divisions: 38,
                          onChanged: (v) =>
                              setState(() => AudioEngine.compressorRatio = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 30,
                    child: Text(
                      '${AudioEngine.compressorRatio.toStringAsFixed(1)}:1',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  TextButton(
                    onPressed: () => setState(() =>
                        _showAdvancedCompressor = !_showAdvancedCompressor),
                    style: TextButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 4),
                      minimumSize: Size.zero,
                      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                    child: Text(
                      _showAdvancedCompressor ? 'Hide' : 'Adv',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.5),
                      ),
                    ),
                  ),
                ],
              ],
            ),
            if (_showAdvancedCompressor && AudioEngine.compressorEnabled) ...[
              const SizedBox(height: 6),
              Row(
                children: [
                  Text('Knee',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorKnee,
                          min: 0.0,
                          max: 12.0,
                          divisions: 24,
                          onChanged: (v) =>
                              setState(() => AudioEngine.compressorKnee = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 30,
                    child: Text(
                      '${AudioEngine.compressorKnee.toStringAsFixed(1)} dB',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text('Att',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorAttack,
                          min: 0.1,
                          max: 100.0,
                          divisions: 99,
                          onChanged: (v) =>
                              setState(() => AudioEngine.compressorAttack = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 30,
                    child: Text(
                      '${AudioEngine.compressorAttack.toStringAsFixed(1)} ms',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text('Rel',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorRelease,
                          min: 10.0,
                          max: 1000.0,
                          divisions: 99,
                          onChanged: (v) =>
                              setState(() => AudioEngine.compressorRelease = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 32,
                    child: Text(
                      '${AudioEngine.compressorRelease.toStringAsFixed(0)} ms',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text('Make',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.compressorMakeup,
                          min: 0.0,
                          max: 24.0,
                          divisions: 48,
                          onChanged: (v) =>
                              setState(() => AudioEngine.compressorMakeup = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 24,
                    child: Text(
                      '${AudioEngine.compressorMakeup.toStringAsFixed(1)} dB',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                ],
              ),
            ],
            const SizedBox(height: 6),
            const Divider(height: 1, color: Colors.white12),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Reverb',
                    style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.5))),
                const SizedBox(width: 6),
                SizedBox(
                  height: 22,
                  child: Switch.adaptive(
                    value: AudioEngine.reverbEnabled,
                    onChanged: (v) =>
                        setState(() => AudioEngine.reverbEnabled = v),
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  ),
                ),
                if (AudioEngine.reverbEnabled) ...[
                  const SizedBox(width: 4),
                  Text('Mix',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.reverbMix,
                          min: 0.0,
                          max: 1.0,
                          divisions: 20,
                          onChanged: (v) =>
                              setState(() => AudioEngine.reverbMix = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 30,
                    child: Text(
                      '${(AudioEngine.reverbMix * 100).toStringAsFixed(0)}%',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 4),
                  Text('Decay',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.reverbDecay,
                          min: 0.1,
                          max: 10.0,
                          divisions: 99,
                          onChanged: (v) =>
                              setState(() => AudioEngine.reverbDecay = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 28,
                    child: Text(
                      '${AudioEngine.reverbDecay.toStringAsFixed(1)}s',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 4),
                  Text('Room',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.reverbRoomSize,
                          min: 0.0,
                          max: 1.0,
                          divisions: 20,
                          onChanged: (v) =>
                              setState(() => AudioEngine.reverbRoomSize = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 24,
                    child: Text(
                      '${(AudioEngine.reverbRoomSize * 100).toStringAsFixed(0)}%',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 4),
                  Text('Damp',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.reverbDamping,
                          min: 0.0,
                          max: 1.0,
                          divisions: 20,
                          onChanged: (v) =>
                              setState(() => AudioEngine.reverbDamping = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 24,
                    child: Text(
                      '${(AudioEngine.reverbDamping * 100).toStringAsFixed(0)}%',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  const SizedBox(width: 4),
                  Text('Pre',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  Expanded(
                    child: SizedBox(
                      height: 22,
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 5),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 8),
                        ),
                        child: Slider(
                          value: AudioEngine.reverbPreDelay,
                          min: 0.0,
                          max: 200.0,
                          divisions: 40,
                          onChanged: (v) =>
                              setState(() => AudioEngine.reverbPreDelay = v),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 30,
                    child: Text(
                      '${AudioEngine.reverbPreDelay.toStringAsFixed(0)}ms',
                      style: TextStyle(
                        fontSize: 9,
                        color: Colors.white.withValues(alpha: 0.4),
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                ],
              ],
            ),
            const SizedBox(height: 6),
            const Divider(height: 1, color: Colors.white12),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Crossfade',
                    style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.5))),
                const SizedBox(width: 6),
                Expanded(
                  child: SizedBox(
                    height: 22,
                    child: SliderTheme(
                      data: SliderTheme.of(context).copyWith(
                        trackHeight: 2,
                        thumbShape:
                            const RoundSliderThumbShape(enabledThumbRadius: 5),
                        overlayShape:
                            const RoundSliderOverlayShape(overlayRadius: 8),
                      ),
                      child: Slider(
                        value: AudioEngine.crossfadeMs,
                        min: 0.0,
                        max: 170.0,
                        divisions: 170,
                        onChanged: (v) =>
                            setState(() => AudioEngine.crossfadeMs = v),
                      ),
                    ),
                  ),
                ),
                SizedBox(
                  width: 36,
                  child: Text(
                    AudioEngine.crossfadeMs == 0
                        ? 'Off'
                        : '${AudioEngine.crossfadeMs.toStringAsFixed(0)}ms',
                    style: TextStyle(
                      fontSize: 9,
                      color: Colors.white.withValues(alpha: 0.4),
                      fontFamily: 'monospace',
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 6),
            const Divider(height: 1, color: Colors.white12),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Audio Focus',
                    style: TextStyle(
                        fontSize: 11,
                        color: Colors.white.withValues(alpha: 0.5))),
                const SizedBox(width: 6),
                SizedBox(
                  height: 22,
                  child: Switch.adaptive(
                    value: AudioEngine.audioFocusEnabled,
                    onChanged: (v) =>
                        setState(() => AudioEngine.audioFocusEnabled = v),
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  ),
                ),
                if (AudioEngine.audioFocusEnabled) ...[
                  const SizedBox(width: 8),
                  Text('Notif',
                      style: TextStyle(
                          fontSize: 9,
                          color: Colors.white.withValues(alpha: 0.35))),
                  SizedBox(
                    height: 22,
                    child: Switch.adaptive(
                      value: AudioEngine.pauseOnNotification,
                      onChanged: (v) =>
                          setState(() => AudioEngine.pauseOnNotification = v),
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                  ),
                ],
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
