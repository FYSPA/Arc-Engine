package com.fyspa.audio_engine

import io.flutter.embedding.engine.plugins.FlutterPlugin

class AudioEnginePlugin : FlutterPlugin {
    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        // FFI puro, no necesitamos registrar nada aquí
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        // Limpieza si es necesaria
    }
}