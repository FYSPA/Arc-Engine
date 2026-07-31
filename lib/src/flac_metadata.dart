// ---------------------------------------------------------------------------
// File: flac_metadata.dart
// Purpose: Data class for FLAC file metadata (tags + technical properties).
//          Separated from audio_mixer.dart to avoid circular imports between
//          audio_mixer.dart and track_player.dart.
// Importance: Used by both AudioEngine.getFlacMetadata() and TrackPlayer getters.
// ---------------------------------------------------------------------------

import 'dart:convert';
import 'dart:ffi';

/// Descriptive + technical metadata for a FLAC file.
///
/// Obtained via [AudioEngine.getFlacMetadata] or [TrackPlayer] getters
/// after playback starts. Tags come from Vorbis Comments embedded in the
/// FLAC file. Fields not present in the file are empty strings or zero.
class FlacMetadataData {
  final int sampleRate;
  final int bitDepth;
  final int channels;
  final int totalSamples;
  final int bitrate;
  final String title;
  final String titleClean;
  final String artist;
  final String album;
  final String isrc;
  final int? trackNumber;
  final int? year;
  final Duration duration;

  const FlacMetadataData({
    required this.sampleRate,
    required this.bitDepth,
    required this.channels,
    required this.totalSamples,
    required this.bitrate,
    required this.title,
    required this.titleClean,
    required this.artist,
    required this.album,
    required this.isrc,
    this.trackNumber,
    this.year,
    required this.duration,
  });

  @override
  String toString() =>
      'FlacMetadataData(title: "$title", artist: "$artist", album: "$album", '
      '${sampleRate}Hz/${bitDepth}bit/$channels ch, ${duration.inSeconds}s)';
}

/// Converts a fixed-size C char array (Array<Uint8>) to a Dart string.
String arrayToString(Array<Uint8> arr) {
  final codes = <int>[];
  try {
    for (int i = 0; i < 256; i++) {
      final byte = arr[i];
      if (byte == 0) break;
      codes.add(byte);
    }
  } on RangeError {
    // Reached end of smaller array (e.g. isrc[16])
  }
  return codes.isEmpty ? '' : utf8.decode(codes);
}

/// Strips numeric prefixes from a title.
///
/// Examples: "01 - Song Name" → "Song Name", "01.Song Name" → "Song Name"
/// Preserves titles starting with non-separator chars like "&burn", "(ecco)".
String computeTitleClean(String title) {
  return title.replaceFirst(RegExp(r'^\d+[\s\-._]+'), '').trim();
}
