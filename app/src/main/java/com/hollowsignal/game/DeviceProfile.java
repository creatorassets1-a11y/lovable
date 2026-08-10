package com.hollowsignal.game;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.os.Build;

/**
 * Works out roughly what the device can cope with, before a single frame is
 * drawn.
 *
 * The engine also measures its own framerate and adapts, but that only helps
 * after the player has already seen a bad first few seconds. Starting at a
 * sensible tier means a weak device never renders a 1080p frame it cannot
 * afford, and a strong one does not spend ten seconds climbing back up.
 */
public final class DeviceProfile {

    /** Tiers match the values consumed in jnibridge.cpp. */
    public static final int TIER_LOW = 0;
    public static final int TIER_MID = 1;
    public static final int TIER_HIGH = 2;

    private static int sTier = TIER_MID;
    private static boolean sProbed = false;

    private DeviceProfile() { }

    public static void probe(Context context) {
        if (sProbed || context == null) {
            return;
        }
        sProbed = true;

        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        long totalMemMb = 2048;
        boolean lowRam = false;
        int glesMajor = 3;

        if (am != null) {
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            totalMemMb = mi.totalMem / (1024L * 1024L);
            lowRam = am.isLowRamDevice();
            ConfigurationInfo ci = am.getDeviceConfigurationInfo();
            if (ci != null) {
                glesMajor = (ci.reqGlEsVersion >> 16) & 0xFFFF;
            }
        }

        int cores = Runtime.getRuntime().availableProcessors();

        if (lowRam || totalMemMb < 2200 || cores <= 4 || glesMajor < 3) {
            sTier = TIER_LOW;
        } else if (totalMemMb >= 5800 && cores >= 8 && Build.VERSION.SDK_INT >= 26) {
            sTier = TIER_HIGH;
        } else {
            sTier = TIER_MID;
        }
    }

    /** Invoked from native code at start-up. */
    public static int tier() {
        return sTier;
    }
}
