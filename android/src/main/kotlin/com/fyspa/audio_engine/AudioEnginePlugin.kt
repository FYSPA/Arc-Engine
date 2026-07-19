package com.fyspa.audio_engine

import android.app.Activity
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class AudioEnginePlugin : FlutterPlugin, MethodChannel.MethodCallHandler, EventChannel.StreamHandler, ActivityAware {

    private var methodChannel: MethodChannel? = null
    private var eventChannel: EventChannel? = null
    private var eventSink: EventChannel.EventSink? = null
    private var audioManager: AudioManager? = null
    private var activity: Activity? = null
    private var pauseOnNotification: Boolean = true
    private var audioFocusRequest: AudioFocusRequest? = null
    private val focusChangeHandler = Handler(Looper.getMainLooper())

    private val focusChangeListener = AudioManager.OnAudioFocusChangeListener { focusChange ->
        focusChangeHandler.post {
            val event = when (focusChange) {
                AudioManager.AUDIOFOCUS_GAIN -> "gain"
                AudioManager.AUDIOFOCUS_LOSS -> "loss"
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> "lossTransient"
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> "duck"
                else -> null
            }
            if (event != null) {
                eventSink?.success(event)
            }
        }
    }

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        methodChannel = MethodChannel(binding.binaryMessenger, "com.fyspa.audio_engine/audio_focus")
        methodChannel?.setMethodCallHandler(this)

        eventChannel = EventChannel(binding.binaryMessenger, "com.fyspa.audio_engine/audio_focus_events")
        eventChannel?.setStreamHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        abandonAudioFocus()
        methodChannel?.setMethodCallHandler(null)
        methodChannel = null
        eventChannel?.setStreamHandler(null)
        eventChannel = null
        eventSink = null
        audioManager = null
        activity = null
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        audioManager = activity?.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
    }

    override fun onDetachedFromActivity() {
        abandonAudioFocus()
        activity = null
        audioManager = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        onAttachedToActivity(binding)
    }

    override fun onDetachedFromActivityForConfigChanges() {
        onDetachedFromActivity()
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "requestFocus" -> {
                val success = requestAudioFocus()
                result.success(success)
            }
            "abandonFocus" -> {
                abandonAudioFocus()
                result.success(true)
            }
            "setPauseOnNotification" -> {
                pauseOnNotification = call.arguments as? Boolean ?: true
                result.success(true)
            }
            else -> result.notImplemented()
        }
    }

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        eventSink = events
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
    }

    private fun requestAudioFocus(): Boolean {
        val am = audioManager ?: return false
        try {
            if (audioFocusRequest == null) {
                audioFocusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .build()
                    )
                    .setOnAudioFocusChangeListener(focusChangeListener, focusChangeHandler)
                    .setWillPauseWhenDucked(false)
                    .build()
            }
            val result = am.requestAudioFocus(audioFocusRequest!!)
            return result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        } catch (e: Exception) {
            return false
        }
    }

    private fun abandonAudioFocus() {
        val am = audioManager ?: return
        val req = audioFocusRequest ?: return
        try {
            am.abandonAudioFocusRequest(req)
        } catch (_: Exception) {}
    }
}
