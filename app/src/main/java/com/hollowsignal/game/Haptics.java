package com.hollowsignal.game;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;

/**
 * Vibration, driven from the engine.
 *
 * Called from C++ over JNI at the moments where a physical jolt adds something
 * the speakers cannot: a light blowing out beside you, a flare firing, the
 * thing reaching you. Deliberately sparse - haptics used constantly stop being
 * a signal and become a battery drain.
 */
public final class Haptics {

    /** Must match the HapticId enum in jnibridge.cpp. */
    public static final int HAPTIC_LIGHT_POP = 0;
    public static final int HAPTIC_FLARE = 1;
    public static final int HAPTIC_NEAR_MISS = 2;
    public static final int HAPTIC_DEATH = 3;
    public static final int HAPTIC_PICKUP = 4;

    private static Vibrator sVibrator;
    private static boolean sHasAmplitudeControl;

    private Haptics() { }

    public static void init(Context context) {
        if (context == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibratorManager vm =
                    (VibratorManager) context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            sVibrator = (vm != null) ? vm.getDefaultVibrator() : null;
        } else {
            sVibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        }
        sHasAmplitudeControl = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && sVibrator != null
                && sVibrator.hasAmplitudeControl();
    }

    /** Invoked from native code. Silently does nothing when there is no motor. */
    public static void play(int id) {
        Vibrator v = sVibrator;
        if (v == null || !v.hasVibrator()) {
            return;
        }
        long[] pattern;
        int[] amplitudes;
        switch (id) {
            case HAPTIC_LIGHT_POP:
                pattern = new long[] { 0, 26 };
                amplitudes = new int[] { 0, 190 };
                break;
            case HAPTIC_FLARE:
                pattern = new long[] { 0, 18, 40, 70 };
                amplitudes = new int[] { 0, 255, 0, 110 };
                break;
            case HAPTIC_NEAR_MISS:
                // Two soft taps: felt, not heard. This is the one the player
                // never consciously notices and always reacts to.
                pattern = new long[] { 0, 14, 90, 14 };
                amplitudes = new int[] { 0, 90, 0, 70 };
                break;
            case HAPTIC_DEATH:
                pattern = new long[] { 0, 220, 60, 400 };
                amplitudes = new int[] { 0, 255, 0, 200 };
                break;
            case HAPTIC_PICKUP:
                pattern = new long[] { 0, 12 };
                amplitudes = new int[] { 0, 80 };
                break;
            default:
                return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (sHasAmplitudeControl) {
                v.vibrate(VibrationEffect.createWaveform(pattern, amplitudes, -1));
            } else {
                v.vibrate(VibrationEffect.createWaveform(pattern, -1));
            }
        } else {
            v.vibrate(pattern, -1);
        }
    }
}
