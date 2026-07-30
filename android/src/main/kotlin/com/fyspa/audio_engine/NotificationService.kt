package com.fyspa.audio_engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build
import android.os.IBinder
import android.content.pm.ServiceInfo
import java.net.URL
import kotlin.concurrent.thread

class NotificationService : Service() {

    private var mediaSession: MediaSession? = null
    private var notificationManager: NotificationManager? = null
    private var commandHandler: ((String, Any?) -> Unit)? = null
    private var isForeground = false

    companion object {
        const val CHANNEL_ID = "arc_audio_playback"
        const val NOTIFICATION_ID = 1
        const val ACTION_PLAY = "com.fyspa.audio_engine.PLAY"
        const val ACTION_PAUSE = "com.fyspa.audio_engine.PAUSE"
        const val ACTION_NEXT = "com.fyspa.audio_engine.NEXT"
        const val ACTION_PREV = "com.fyspa.audio_engine.PREV"
        const val ACTION_STOP = "com.fyspa.audio_engine.STOP"

        var instance: NotificationService? = null
            private set

        // Static handler survives service recreation (START_STICKY)
        var staticCommandHandler: ((String, Any?) -> Unit)? = null

        // Cached last-known title/artist for native fallback when Flutter is dead
        var lastTitle: String = "Arc Audio"
        var lastArtist: String = ""
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
        notificationManager = getSystemService(NotificationManager::class.java)
        createNotificationChannel()
        startForegroundSafe(buildPlaceholderNotification())
        setupMediaSession()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Ensure handler is set (survives service recreation via static field)
        if (commandHandler == null) commandHandler = staticCommandHandler

        when (intent?.action) {
            ACTION_PLAY -> commandHandler?.invoke("play", null)
            ACTION_PAUSE -> commandHandler?.invoke("pause", null)
            ACTION_NEXT -> commandHandler?.invoke("next", null)
            ACTION_PREV -> commandHandler?.invoke("previous", null)
            ACTION_STOP -> commandHandler?.invoke("stop", null)
        }
        return START_STICKY
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        mediaSession?.release()
        mediaSession = null
        instance = null
        super.onDestroy()
    }

    fun setCommandHandler(handler: (String, Any?) -> Unit) {
        commandHandler = handler
        staticCommandHandler = handler
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Audio Playback",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Arc Audio Engine playback controls"
                setShowBadge(false)
                lockscreenVisibility = Notification.VISIBILITY_PUBLIC
            }
            notificationManager?.createNotificationChannel(channel)
        }
    }

    private fun buildPlaceholderNotification(): Notification {
        val style = Notification.MediaStyle()
            .setShowActionsInCompactView(1)

        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Arc Audio")
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setStyle(style)
            .build()
    }

    private fun startForegroundSafe(notification: Notification) {
        if (!isForeground) {
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
                } else {
                    startForeground(NOTIFICATION_ID, notification)
                }
                isForeground = true
            } catch (_: SecurityException) {
                startForeground(NOTIFICATION_ID, notification)
                isForeground = true
            }
        } else {
            notificationManager?.notify(NOTIFICATION_ID, notification)
        }
    }

    private fun setupMediaSession() {
        mediaSession = MediaSession(this, "ArcAudioSession").apply {
            setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS or MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS)
            setCallback(object : MediaSession.Callback() {
                override fun onPlay() {
                    commandHandler?.invoke("play", null)
                }

                override fun onPause() {
                    commandHandler?.invoke("pause", null)
                }

                override fun onSkipToNext() {
                    commandHandler?.invoke("next", null)
                }

                override fun onSkipToPrevious() {
                    commandHandler?.invoke("previous", null)
                }

                override fun onStop() {
                    commandHandler?.invoke("stop", null)
                }

                override fun onSeekTo(pos: Long) {
                    commandHandler?.invoke("seekTo", pos)
                }
            })
            isActive = true
        }
    }

    fun updateMetadata(title: String, artist: String, album: String, durationMs: Long) {
        val builder = MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, artist)
            .putString(MediaMetadata.METADATA_KEY_ALBUM, album)
            .putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs)
        mediaSession?.setMetadata(builder.build())
    }

    fun updateArtwork(bitmap: Bitmap?) {
        if (bitmap != null) {
            val current = mediaSession?.controller?.metadata
            val builder = if (current != null) {
                MediaMetadata.Builder(current)
            } else {
                MediaMetadata.Builder()
            }
            builder.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, bitmap)
            mediaSession?.setMetadata(builder.build())
        }
    }

    fun updatePlaybackState(isPlaying: Boolean, positionMs: Long, speed: Float = 1.0f) {
        val state = if (isPlaying) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED
        val playbackState = PlaybackState.Builder()
            .setActions(
                PlaybackState.ACTION_PLAY or
                PlaybackState.ACTION_PAUSE or
                PlaybackState.ACTION_PLAY_PAUSE or
                PlaybackState.ACTION_SKIP_TO_NEXT or
                PlaybackState.ACTION_SKIP_TO_PREVIOUS or
                PlaybackState.ACTION_SEEK_TO or
                PlaybackState.ACTION_STOP
            )
            .setState(state, positionMs, speed)
            .build()
        mediaSession?.setPlaybackState(playbackState)
    }

    fun showNotification(title: String, artist: String, isPlaying: Boolean) {
        lastTitle = title
        lastArtist = artist
        val sessionToken = mediaSession?.sessionToken ?: return

        val contentIntent = packageManager
            .getLaunchIntentForPackage(packageName)
            ?.let { PendingIntent.getActivity(
                this, 0, it,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )}

        val prevIntent = PendingIntent.getService(
            this, 1, Intent(this, NotificationService::class.java).apply { action = ACTION_PREV },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val nextIntent = PendingIntent.getService(
            this, 2, Intent(this, NotificationService::class.java).apply { action = ACTION_NEXT },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val playPauseIntent = PendingIntent.getService(
            this, 3, Intent(this, NotificationService::class.java).apply {
                action = if (isPlaying) ACTION_PAUSE else ACTION_PLAY
            },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stopIntent = PendingIntent.getService(
            this, 4, Intent(this, NotificationService::class.java).apply { action = ACTION_STOP },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val playPauseIcon = if (isPlaying)
            android.R.drawable.ic_media_pause
        else
            android.R.drawable.ic_media_play

        val style = Notification.MediaStyle()
            .setMediaSession(sessionToken)
            .setShowActionsInCompactView(0, 1, 2)

        val builder = Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(title)
            .setContentText(artist)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setStyle(style)
            .addAction(Notification.Action.Builder(
                android.R.drawable.ic_media_previous, "Previous", prevIntent
            ).build())
            .addAction(Notification.Action.Builder(
                playPauseIcon, if (isPlaying) "Pause" else "Play", playPauseIntent
            ).build())
            .addAction(Notification.Action.Builder(
                android.R.drawable.ic_media_next, "Next", nextIntent
            ).build())
            .addAction(Notification.Action.Builder(
                android.R.drawable.ic_menu_close_clear_cancel, "Stop", stopIntent
            ).build())

        val art = mediaSession?.controller?.metadata?.getBitmap(
            MediaMetadata.METADATA_KEY_ALBUM_ART
        )
        if (art != null) {
            builder.setLargeIcon(art)
        }

        startForegroundSafe(builder.build())
    }

    fun hideNotification() {
        stopForeground(STOP_FOREGROUND_REMOVE)
    }
}
