package com.hollowsignal.game

import android.app.NativeActivity
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager

/**
 * Hosts the native engine.
 *
 * The game itself is C++ behind NativeActivity; this subclass owns the things
 * that are genuinely the platform's job and are painful to reach from JNI:
 * immersive mode, keeping the screen awake, and audio focus. It also mirrors
 * the native save file into SharedPreferences so the rest of the app can read
 * progress without parsing the engine's binary format.
 */
class MainActivity : NativeActivity() {

    private var audioManager: AudioManager? = null
    private var focusRequest: AudioFocusRequest? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // A horror game that dims to the lock screen mid-stalk is not scary,
        // it is annoying.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        Haptics.init(this)
        DeviceProfile.probe(this)
        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
    }

    override fun onResume() {
        super.onResume()
        goImmersive()
        requestAudioFocus()
    }

    override fun onPause() {
        abandonAudioFocus()
        Progress.sync(this, nativeMissionIndex(), nativeMissionsUnlocked())
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) goImmersive()
    }

    /** Hide the system bars, and let them come back on a swipe rather than a tap. */
    private fun goImmersive() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { c ->
                c.hide(android.view.WindowInsets.Type.systemBars())
                c.systemBarsBehavior =
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                )
        }
    }

    /**
     * Ask for exclusive playback. The mix is built around quiet stretches, so
     * another app's audio underneath it does real damage to the pacing.
     */
    private fun requestAudioFocus() {
        val am = audioManager ?: return
        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
            .build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val req = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attrs)
                .setWillPauseWhenDucked(false)
                .build()
            focusRequest = req
            am.requestAudioFocus(req)
        } else {
            @Suppress("DEPRECATION")
            am.requestAudioFocus(null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
        }
    }

    private fun abandonAudioFocus() {
        val am = audioManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            focusRequest?.let { am.abandonAudioFocusRequest(it) }
        } else {
            @Suppress("DEPRECATION")
            am.abandonAudioFocus(null)
        }
        focusRequest = null
    }

    // Implemented in jnibridge.cpp. They return -1 if the engine has not
    // started yet, which is why Progress.sync ignores negatives.
    private external fun nativeMissionIndex(): Int
    private external fun nativeMissionsUnlocked(): Int

    companion object {
        init {
            System.loadLibrary("hollowsignal")
        }
    }
}
