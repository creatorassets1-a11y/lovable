package com.hollowsignal.game

import android.content.Context

/**
 * Mirror of the engine's save state in SharedPreferences.
 *
 * The engine owns the real save file (a few ints in internal storage). This
 * copy exists so platform-side code can show progress without linking against
 * the engine or parsing its format, and so a reinstall-safe backup has
 * something to pick up.
 */
object Progress {
    private const val PREFS = "hollow_progress"
    private const val KEY_MISSION = "mission_index"
    private const val KEY_UNLOCKED = "missions_unlocked"

    @JvmStatic
    fun sync(context: Context, missionIndex: Int, unlocked: Int) {
        // Negative means the engine has not reported yet; do not clobber a
        // good value with a placeholder.
        if (missionIndex < 0 && unlocked < 0) return
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            if (missionIndex >= 0) putInt(KEY_MISSION, missionIndex)
            if (unlocked >= 0) putInt(KEY_UNLOCKED, unlocked)
            apply()
        }
    }

    @JvmStatic
    fun unlocked(context: Context): Int =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY_UNLOCKED, 1)

    @JvmStatic
    fun missionIndex(context: Context): Int =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY_MISSION, 0)
}
