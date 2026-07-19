import 'dart:async';
import 'package:flutter/services.dart';

/// Types of audio focus change events from the Android AudioManager.
///
/// Maps directly to [AudioManager.OnAudioFocusChangeListener] constants.
enum AudioFocusEvent {
  /// We regained audio focus. Resume playback / restore volume.
  gain,

  /// We lost audio focus permanently (another app started playback).
  /// Do NOT auto-resume — user must tap play again.
  loss,

  /// We lost audio focus temporarily (e.g. incoming call, notification).
  /// Auto-resume when [gain] is received.
  lossTransient,

  /// Another app wants to play over us temporarily at reduced volume.
  /// Lower volume until [gain].
  duck,
}

/// Platform-channel bridge to Android AudioManager for audio focus.
///
/// Requests and abandons audio focus via the Kotlin plugin, and exposes
/// an [events] stream for focus change notifications.
///
/// Typically used through [AudioEngine]'s auto-handler; you only need to
/// interact with this class directly if you want custom behavior.
class AudioFocus {
  static const _methodChannel =
      MethodChannel('com.fyspa.audio_engine/audio_focus');
  static const _eventChannel =
      EventChannel('com.fyspa.audio_engine/audio_focus_events');

  static Stream<AudioFocusEvent>? _cachedStream;

  /// Broadcast stream of [AudioFocusEvent] from the Android AudioManager.
  ///
  /// Events are emitted on the main thread. The stream is lazily created
  /// on first access and reused for the lifetime of the app.
  static Stream<AudioFocusEvent> get events {
    _cachedStream ??= _eventChannel.receiveBroadcastStream().map((event) {
      return AudioFocusEvent.values.firstWhere(
        (e) => e.name == event,
        orElse: () => AudioFocusEvent.gain,
      );
    });
    return _cachedStream!;
  }

  /// Requests audio focus from Android's AudioManager.
  ///
  /// Returns `true` if focus was granted, `false` otherwise.
  /// Returns `false` silently if the platform channel is unavailable (e.g., tests).
  static Future<bool> request() async {
    try {
      return await _methodChannel.invokeMethod<bool>('requestFocus') ?? false;
    } catch (_) {
      return false;
    }
  }

  /// Abandons audio focus. Call when all tracks have stopped.
  static Future<void> abandon() async {
    try {
      await _methodChannel.invokeMethod('abandonFocus');
    } catch (_) {}
  }

  /// Sends the current [pauseOnNotification] setting to the plugin.
  ///
  /// The plugin doesn't currently use this value (handling is done in Dart),
  /// but it's exposed for future platform-side logic.
  static Future<void> setPauseOnNotification(bool v) async {
    try {
      await _methodChannel.invokeMethod('setPauseOnNotification', v);
    } catch (_) {}
  }
}
