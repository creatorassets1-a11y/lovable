package com.blackmoor.matron

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager

/**
 * Thin shell: immersive fullscreen, a wake lock, safe-area insets, and lifecycle
 * forwarding. Everything else lives in the native engine.
 */
class MainActivity : Activity() {

    private lateinit var view: GameView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }

        view = GameView(this)
        setContentView(view)

        // Notch and gesture-bar insets, handed to the HUD so nothing it draws
        // ends up underneath them.
        view.setOnApplyWindowInsetsListener { _, insets ->
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val bars = insets.getInsets(
                    android.view.WindowInsets.Type.systemBars() or
                        android.view.WindowInsets.Type.displayCutout()
                )
                view.applyInsets(
                    bars.left.toFloat(), bars.top.toFloat(),
                    bars.right.toFloat(), bars.bottom.toFloat()
                )
            } else {
                @Suppress("DEPRECATION")
                view.applyInsets(
                    insets.systemWindowInsetLeft.toFloat(),
                    insets.systemWindowInsetTop.toFloat(),
                    insets.systemWindowInsetRight.toFloat(),
                    insets.systemWindowInsetBottom.toFloat()
                )
            }
            insets
        }
    }

    private fun goImmersive() {
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

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) goImmersive()
    }

    override fun onResume() {
        super.onResume()
        goImmersive()
        view.onResume()
        view.queueEvent { Native.onResume() }
    }

    override fun onPause() {
        view.queueEvent { Native.onPause() }
        view.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        view.queueEvent { Native.onDestroy() }
        super.onDestroy()
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        // Back pauses rather than quitting mid-chapter.
        view.queueEvent { Native.press(2) }
    }
}
