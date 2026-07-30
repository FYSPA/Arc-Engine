package com.fyspa.audio_engine

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioDeviceCallback
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.net.URL
import kotlin.concurrent.thread

class AudioEnginePlugin : FlutterPlugin, MethodChannel.MethodCallHandler, EventChannel.StreamHandler, ActivityAware {

    private var methodChannel: MethodChannel? = null
    private var eventChannel: EventChannel? = null
    private var eventSink: EventChannel.EventSink? = null
    private var audioManager: AudioManager? = null
    private var activity: Activity? = null
    private var context: Context? = null
    private var pauseOnNotification: Boolean = true
    private var audioFocusRequest: AudioFocusRequest? = null
    private var deviceCallback: AudioDeviceCallback? = null
    private val focusChangeHandler = Handler(Looper.getMainLooper())

    // Media Session
    private var mediaMethodChannel: MethodChannel? = null
    private var notificationService: NotificationService? = null

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
        context = binding.applicationContext

        methodChannel = MethodChannel(binding.binaryMessenger, "com.fyspa.audio_engine/audio_focus")
        methodChannel?.setMethodCallHandler(this)

        eventChannel = EventChannel(binding.binaryMessenger, "com.fyspa.audio_engine/audio_focus_events")
        eventChannel?.setStreamHandler(this)

        mediaMethodChannel = MethodChannel(binding.binaryMessenger, "com.fyspa.audio_engine/media_session")
        mediaMethodChannel?.setMethodCallHandler(this)
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
        context = null
        mediaMethodChannel?.setMethodCallHandler(null)
        mediaMethodChannel = null
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        audioManager = activity?.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        registerDeviceCallback()
    }

    override fun onDetachedFromActivity() {
        abandonAudioFocus()
        unregisterDeviceCallback()
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
            // ─── Audio Focus ───
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

            // ─── Media Session ───
            "setMetadata" -> {
                val args = call.arguments as? Map<*, *> ?: return result.error("INVALID_ARGS", "null args", null)
                val title = args["title"] as? String ?: ""
                val artist = args["artist"] as? String ?: ""
                val album = args["album"] as? String ?: ""
                val durationMs = (args["durationMs"] as? Number)?.toLong() ?: 0L

                notificationService?.updateMetadata(title, artist, album, durationMs)
                result.success(true)
            }
            "setPlaybackState" -> {
                val args = call.arguments as? Map<*, *> ?: return result.error("INVALID_ARGS", "null args", null)
                val isPlaying = args["isPlaying"] as? Boolean ?: false
                val positionMs = (args["positionMs"] as? Number)?.toLong() ?: 0L
                val speed = (args["speed"] as? Number)?.toFloat() ?: 1.0f

                notificationService?.updatePlaybackState(isPlaying, positionMs, speed)
                result.success(true)
            }
            "setArtwork" -> {
                val path = call.arguments as? String
                if (path != null && path.isNotEmpty()) {
                    thread {
                        try {
                            val bitmap: Bitmap? = when {
                                path.startsWith("http://") || path.startsWith("https://") -> {
                                    val url = URL(path)
                                    val connection = url.openConnection()
                                    connection.connectTimeout = 5000
                                    connection.readTimeout = 5000
                                    val inputStream = connection.getInputStream()
                                    val bmp = BitmapFactory.decodeStream(inputStream)
                                    inputStream.close()
                                    bmp
                                }
                                path.startsWith("/") -> {
                                    BitmapFactory.decodeFile(path)
                                }
                                else -> null
                            }
                            if (bitmap != null) {
                                // Scale down to avoid oversized notifications (max 320x320)
                                val scaled = Bitmap.createScaledBitmap(bitmap, 320, 320, true)
                                if (scaled !== bitmap) bitmap.recycle()
                                focusChangeHandler.post {
                                    notificationService?.updateArtwork(scaled)
                                }
                            }
                        } catch (e: Exception) {
                            e.printStackTrace()
                        }
                    }
                }
                result.success(true)
            }
            "show" -> {
                val args = call.arguments as? Map<*, *> ?: mapOf<String, Any>()
                val title = args["title"] as? String ?: ""
                val artist = args["artist"] as? String ?: ""
                val isPlaying = args["isPlaying"] as? Boolean ?: false

                ensureNotificationService {
                    notificationService?.showNotification(title, artist, isPlaying)
                }
                result.success(true)
            }
            "hide" -> {
                notificationService?.hideNotification()
                result.success(true)
            }
            "requestNotificationPermission" -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    val ctx = context ?: activity ?: return result.success(false)
                    if (androidx.core.content.ContextCompat.checkSelfPermission(ctx, "android.permission.POST_NOTIFICATIONS")
                        == PackageManager.PERMISSION_GRANTED) {
                        result.success(true)
                    } else if (activity != null) {
                        androidx.core.app.ActivityCompat.requestPermissions(
                            activity!!,
                            arrayOf("android.permission.POST_NOTIFICATIONS"),
                            1001
                        )
                        result.success(false)
                    } else {
                        result.success(false)
                    }
                } else {
                    result.success(true)
                }
            }
            "ensureService" -> {
                ensureNotificationService()
                result.success(true)
            }
            else -> result.notImplemented()
        }
    }

    private fun ensureNotificationService(onReady: (() -> Unit)? = null) {
        // Check if service is actually still alive AND same instance
        if (notificationService != null && NotificationService.instance != null
            && notificationService === NotificationService.instance) {
            onReady?.invoke()
            return
        }

        // Service was destroyed or is a different instance — clear stale reference
        notificationService = null

        val ctx = context ?: return
        val intent = Intent(ctx, NotificationService::class.java)
        ctx.startForegroundService(intent)

        // Try to grab the service instance immediately (set in onCreate)
        notificationService = NotificationService.instance
        if (notificationService != null) {
            attachCommandHandler()
            onReady?.invoke()
            return
        }

        // Poll until service is ready (onCreate runs before onStartCommand)
        val poll = object : Runnable {
            override fun run() {
                notificationService = NotificationService.instance
                if (notificationService != null) {
                    attachCommandHandler()
                    onReady?.invoke()
                } else {
                    focusChangeHandler.postDelayed(this, 50)
                }
            }
        }
        focusChangeHandler.postDelayed(poll, 50)
    }

    private fun attachCommandHandler() {
        notificationService?.setCommandHandler { action, data ->
            if (mediaMethodChannel != null) {
                // Flutter engine alive — forward to Dart
                focusChangeHandler.post {
                    val args = mutableMapOf<String, Any?>("action" to action)
                    if (data != null) args["data"] = data
                    mediaMethodChannel?.invokeMethod("onCommand", args)
                }
            } else {
                // Flutter engine dead — handle natively via JNI
                when (action) {
                    "play" -> NativeBridge.resumeAll()
                    "pause" -> NativeBridge.pauseAll()
                    "stop" -> NativeBridge.stopAll()
                }
                // Update notification with cached title/artist
                val svc = NotificationService.instance
                if (svc != null) {
                    val playing = NativeBridge.isAnyPlaying()
                    focusChangeHandler.post {
                        svc.showNotification(
                            NotificationService.lastTitle,
                            NotificationService.lastArtist,
                            playing
                        )
                    }
                }
            }
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

    private fun registerDeviceCallback() {
        val am = audioManager ?: return
        deviceCallback = object : AudioDeviceCallback() {
            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
                for (device in removedDevices) {
                    if (isBluetoothDevice(device.type)) {
                        focusChangeHandler.postDelayed({
                            eventSink?.success("becomingNoisy")
                        }, 1500)
                        return
                    }
                }
            }

            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
                // No-op: BT connect does NOT trigger pause
            }
        }
        am.registerAudioDeviceCallback(deviceCallback, focusChangeHandler)
    }

    private fun unregisterDeviceCallback() {
        deviceCallback?.let {
            try { audioManager?.unregisterAudioDeviceCallback(it) } catch (_: Exception) {}
        }
        deviceCallback = null
    }

    companion object {
        private fun isBluetoothDevice(type: Int): Boolean {
            return type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                   type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                   type == AudioDeviceInfo.TYPE_BLE_HEADSET ||
                   type == AudioDeviceInfo.TYPE_BLE_SPEAKER
        }
    }
}
