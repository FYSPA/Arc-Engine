package com.fyspa.audio_engine

object NativeBridge {
    init {
        System.loadLibrary("audio_engine")
    }

    @JvmStatic external fun pauseAll()
    @JvmStatic external fun resumeAll()
    @JvmStatic external fun stopAll()
    @JvmStatic external fun isAnyPlaying(): Boolean
}
