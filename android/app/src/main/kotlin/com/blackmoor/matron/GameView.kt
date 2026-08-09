package com.blackmoor.matron

import android.annotation.SuppressLint
import android.content.Context
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/**
 * GL surface plus the touch model.
 *
 * The left third of the screen is a floating movement stick that appears
 * wherever the thumb lands; the right side is a look-drag; the buttons are hit
 * rectangles derived from the same layout constants the HUD draws with, so what
 * you see and what you can press stay in agreement.
 */
class GameView(context: Context) : GLSurfaceView(context) {

    private var viewW = 1
    private var viewH = 1
    private var unit = 1f

    private var insetL = 0f
    private var insetR = 0f
    private var insetT = 0f
    private var insetB = 0f

    // Movement stick
    private var stickPointer = -1
    private var stickOx = 0f
    private var stickOy = 0f
    private var stickRadius = 1f

    // Look drag
    private var lookPointer = -1
    private var lookX = 0f
    private var lookY = 0f

    private var runHeld = false
    private var crouchHeld = false
    private var runPointer = -1
    private var crouchPointer = -1

    var lookSensitivity = 0.0038f

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 0, 24, 0)
        preserveEGLContextOnPause = true
        setRenderer(object : Renderer {
            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                Native.setAssetManager(context.assets)
                Native.onSurfaceCreated(max(1, viewW), max(1, viewH))
                Native.setInsets(insetL, insetT, insetR, insetB)
            }

            override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
                viewW = w
                viewH = h
                unit = min(w, h) / 100f
                Native.onSurfaceChanged(w, h)
                Native.setInsets(insetL, insetT, insetR, insetB)
            }

            override fun onDrawFrame(gl: GL10?) {
                Native.onDrawFrame()
            }
        })
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun applyInsets(l: Float, t: Float, r: Float, b: Float) {
        insetL = l; insetT = t; insetR = r; insetB = b
        queueEvent { Native.setInsets(l, t, r, b) }
    }

    // ---- button hit tests, mirroring game_hud.cpp's layout maths

    private fun btnRadius() = min(viewW * 0.075f, unit * 9f)

    private fun hitButton(x: Float, y: Float): Int {
        val r = btnRadius()
        val bx = viewW - insetR - unit * 3f - r * 1.2f
        val by = viewH - insetB - unit * 3f - r * 1.2f
        // Generous touch targets: 1.35x the drawn radius.
        val grab = r * 1.35f
        if (hypot(x - bx, y - by) < grab) return BTN_RUN
        if (hypot(x - bx, y - (by - r * 2.5f)) < grab) return BTN_CROUCH
        if (hypot(x - bx, y - (by - r * 5.0f)) < grab) return BTN_TORCH

        // Interact prompt, centre-bottom.
        val pw = min(viewW * 0.34f, unit * 38f)
        val ph = unit * 8f
        val px = viewW * 0.5f - pw * 0.5f
        val py = viewH - insetB - unit * 3f - r * 1.2f - ph * 0.5f
        if (x in px..(px + pw) && y in (py - unit * 2f)..(py + ph + unit * 2f)) {
            return BTN_INTERACT
        }

        // Pause, top-right corner.
        if (x > viewW - insetR - unit * 14f && y < insetT + unit * 14f) return BTN_PAUSE
        return BTN_NONE
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val action = e.actionMasked
        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i)
                val y = e.getY(i)

                when (hitButton(x, y)) {
                    BTN_RUN -> { runHeld = true; runPointer = id }
                    BTN_CROUCH -> { crouchHeld = true; crouchPointer = id }
                    BTN_TORCH -> Native.press(1)
                    BTN_PAUSE -> Native.press(2)
                    BTN_INTERACT -> Native.press(0)
                    else -> {
                        if (x < viewW * 0.42f && stickPointer < 0) {
                            stickPointer = id
                            stickOx = x
                            stickOy = y
                            stickRadius = min(viewW, viewH) * 0.14f
                        } else if (lookPointer < 0) {
                            lookPointer = id
                            lookX = x
                            lookY = y
                        }
                        // Any touch that is not a button also advances menus and
                        // cards; the engine ignores it while actually playing.
                        Native.press(3)
                    }
                }
            }

            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val x = e.getX(i)
                    val y = e.getY(i)
                    if (id == stickPointer) {
                        var dx = x - stickOx
                        var dy = y - stickOy
                        val d = hypot(dx, dy)
                        if (d > stickRadius) {
                            dx = dx / d * stickRadius
                            dy = dy / d * stickRadius
                        }
                        Native.setMove(dx / stickRadius, -dy / stickRadius)
                    } else if (id == lookPointer) {
                        val dx = x - lookX
                        val dy = y - lookY
                        lookX = x
                        lookY = y
                        Native.addLook(dx * lookSensitivity, dy * lookSensitivity)
                    }
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP,
            MotionEvent.ACTION_CANCEL -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == stickPointer || action == MotionEvent.ACTION_CANCEL) {
                    stickPointer = -1
                    Native.setMove(0f, 0f)
                }
                if (id == lookPointer || action == MotionEvent.ACTION_CANCEL) {
                    lookPointer = -1
                }
                if (id == runPointer || action == MotionEvent.ACTION_CANCEL) {
                    runHeld = false; runPointer = -1
                }
                if (id == crouchPointer || action == MotionEvent.ACTION_CANCEL) {
                    crouchHeld = false; crouchPointer = -1
                }
            }
        }
        Native.setButtons(runHeld, crouchHeld)
        return true
    }

    companion object {
        private const val BTN_NONE = -1
        private const val BTN_RUN = 0
        private const val BTN_CROUCH = 1
        private const val BTN_TORCH = 2
        private const val BTN_INTERACT = 3
        private const val BTN_PAUSE = 4
    }
}
