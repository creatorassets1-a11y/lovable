package com.blackmoor.matron

import android.content.res.AssetManager

/** Every call into the engine. Loaded once, called from the GL thread only
 *  except for the input setters, which are latched on the C++ side. */
object Native {
    init {
        System.loadLibrary("matron")
    }

    external fun setAssetManager(mgr: AssetManager)

    external fun onSurfaceCreated(width: Int, height: Int): Boolean
    external fun onSurfaceChanged(width: Int, height: Int)
    external fun onDrawFrame()
    external fun onPause()
    external fun onResume()
    external fun onDestroy()

    external fun setInsets(left: Float, top: Float, right: Float, bottom: Float)
    external fun setMove(x: Float, y: Float)
    external fun addLook(x: Float, y: Float)
    external fun setButtons(run: Boolean, crouch: Boolean)

    /** 0 interact, 1 torch, 2 pause. */
    external fun press(which: Int)

    /** Drains one queued haptic: ms | (amplitude shl 16), or 0 if none. */
    external fun consumeHaptic(): Int

    external fun getPhase(): Int

    const val PHASE_BOOT = 0
    const val PHASE_TITLE = 1
    const val PHASE_PLAYING = 2
    const val PHASE_PAUSED = 3
    const val PHASE_DEAD = 4
    const val PHASE_CARD = 5
    const val PHASE_ENDING = 6
}
