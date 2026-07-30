import 'dart:async';

import 'package:flutter/services.dart';

/// MediaSession bridge — connects Dart audio state to Android lock screen
/// and notification controls via a platform channel.
///
/// Usage:
/// 1. Call [requestPermission] once on app start (Android 13+).
/// 2. When a track starts playing, call [setMetadata] + [setPlaybackState] + [show].
/// 3. Listen to [onCommand] for user actions (play/pause/next/prev/seek/stop).
/// 4. When all tracks stop, call [hide].
class MediaSession {
  MediaSession._();

  static const _channel = MethodChannel('com.fyspa.audio_engine/media_session');

  static final _commandController = StreamController<MediaCommand>.broadcast();
  static bool _listenerAttached = false;

  /// Stream of commands from the notification / lock screen controls.
  static Stream<MediaCommand> get onCommand => _commandController.stream;

  /// Initialize the command listener (called once, lazily).
  static void _ensureListener() {
    if (_listenerAttached) return;
    _listenerAttached = true;
    _channel.setMethodCallHandler((call) async {
      switch (call.method) {
        case 'onCommand':
          final args = call.arguments as Map?;
          final action = args?['action'] as String? ?? '';
          final data = args?['data'];
          final command = _parseCommand(action, data);
          if (command != null) _commandController.add(command);
          break;
      }
    });
  }

  /// Request notification permission (Android 13+). Returns true if granted.
  static Future<bool> requestPermission() async {
    _ensureListener();
    try {
      return await _channel
              .invokeMethod<bool>('requestNotificationPermission') ??
          true;
    } catch (_) {
      return true;
    }
  }

  /// Pre-start the notification service so it's ready before first playback.
  static Future<void> ensureService() async {
    _ensureListener();
    try {
      await _channel.invokeMethod('ensureService');
    } catch (_) {}
  }

  /// Update track metadata shown on lock screen and notification.
  static Future<void> setMetadata({
    required String title,
    required String artist,
    required String album,
    required int durationMs,
  }) async {
    _ensureListener();
    try {
      await _channel.invokeMethod('setMetadata', {
        'title': title,
        'artist': artist,
        'album': album,
        'durationMs': durationMs,
      });
    } catch (_) {}
  }

  /// Update playback state (playing/paused, position, speed).
  static Future<void> setPlaybackState({
    required bool isPlaying,
    required int positionMs,
    double speed = 1.0,
  }) async {
    try {
      await _channel.invokeMethod('setPlaybackState', {
        'isPlaying': isPlaying,
        'positionMs': positionMs,
        'speed': speed,
      });
    } catch (_) {}
  }

  /// Set album artwork from a URL or local file path.
  /// Download + caching is handled on the Kotlin side.
  static Future<void> setArtwork(String path) async {
    try {
      await _channel.invokeMethod('setArtwork', path);
    } catch (_) {}
  }

  /// Show the persistent notification (Spotify-style — stays until explicitly hidden).
  static Future<void> show({
    required String title,
    required String artist,
    required bool isPlaying,
  }) async {
    _ensureListener();
    try {
      await _channel.invokeMethod('show', {
        'title': title,
        'artist': artist,
        'isPlaying': isPlaying,
      });
    } catch (_) {}
  }

  /// Hide and dismiss the notification.
  static Future<void> hide() async {
    try {
      await _channel.invokeMethod('hide');
    } catch (_) {}
  }

  static MediaCommand? _parseCommand(String action, dynamic data) {
    switch (action) {
      case 'play':
        return const MediaCommand.play();
      case 'pause':
        return const MediaCommand.pause();
      case 'next':
        return const MediaCommand.next();
      case 'previous':
        return const MediaCommand.previous();
      case 'seekTo':
        return MediaCommand.seekTo((data as num?)?.toInt() ?? 0);
      case 'stop':
        return const MediaCommand.stop();
      default:
        return null;
    }
  }
}

/// Represents a user command from notification / lock screen controls.
sealed class MediaCommand {
  const MediaCommand();

  const factory MediaCommand.play() = MediaCommandPlay;
  const factory MediaCommand.pause() = MediaCommandPause;
  const factory MediaCommand.next() = MediaCommandNext;
  const factory MediaCommand.previous() = MediaCommandPrevious;
  const factory MediaCommand.seekTo(int positionMs) = MediaCommandSeekTo;
  const factory MediaCommand.stop() = MediaCommandStop;
}

class MediaCommandPlay extends MediaCommand {
  const MediaCommandPlay();
}

class MediaCommandPause extends MediaCommand {
  const MediaCommandPause();
}

class MediaCommandNext extends MediaCommand {
  const MediaCommandNext();
}

class MediaCommandPrevious extends MediaCommand {
  const MediaCommandPrevious();
}

class MediaCommandSeekTo extends MediaCommand {
  final int positionMs;
  const MediaCommandSeekTo(this.positionMs);
}

class MediaCommandStop extends MediaCommand {
  const MediaCommandStop();
}
