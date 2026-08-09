package com.blackmoor.ninthloop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.webkit.ConsoleMessage;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.IOException;
import java.io.InputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/**
 * Native shell for The Ninth Loop.
 *
 * The game itself is an HTML5 canvas engine living in assets/game. This activity does
 * four things the web layer cannot do for itself: hold the screen in true immersive
 * fullscreen, keep the display awake, drive the vibrator for jump scares, and serve the
 * bundled assets over an https:// origin so the engine can fetch/decode its own audio
 * (a file:// origin blocks XHR, which would kill the Web Audio pipeline).
 */
public class MainActivity extends Activity {

    /** Virtual host the bundled game is served from. Never resolves on the network. */
    private static final String ASSET_HOST = "appassets.androidplatform.net";
    private static final String ASSET_ROOT = "game/";
    private static final String START_URL =
            "https://" + ASSET_HOST + "/index.html";

    private WebView webView;
    private Vibrator vibrator;

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        vibrator = (Vibrator) getSystemService(VIBRATOR_SERVICE);

        webView = new WebView(this);
        webView.setBackgroundColor(0xFF000000);
        webView.setOverScrollMode(View.OVER_SCROLL_NEVER);
        webView.setHorizontalScrollBarEnabled(false);
        webView.setVerticalScrollBarEnabled(false);

        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        s.setLoadWithOverviewMode(false);
        s.setUseWideViewPort(false);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setTextZoom(100);

        webView.setWebViewClient(new AssetWebViewClient(getAssets()));
        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onConsoleMessage(ConsoleMessage m) {
                android.util.Log.d("NinthLoop", m.message() + " @" + m.lineNumber());
                return true;
            }
        });

        webView.addJavascriptInterface(new NativeBridge(), "AndroidHost");

        setContentView(webView);
        webView.loadUrl(START_URL);
    }

    // ---------------------------------------------------------------- immersion

    private void goImmersive() {
        View d = getWindow().getDecorView();
        d.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goImmersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        goImmersive();
        if (webView != null) {
            webView.onResume();
            webView.evaluateJavascript("window.__onNativeResume && window.__onNativeResume()", null);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (webView != null) {
            webView.evaluateJavascript("window.__onNativePause && window.__onNativePause()", null);
            webView.onPause();
        }
        stopVibration();
    }

    @Override
    protected void onDestroy() {
        stopVibration();
        if (webView != null) {
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }

    /** Route hardware/gesture back into the game so it can close menus instead of quitting. */
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && webView != null) {
            webView.evaluateJavascript("window.__onNativeBack && window.__onNativeBack()", null);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void stopVibration() {
        try {
            if (vibrator != null) vibrator.cancel();
        } catch (Throwable ignored) {
        }
    }

    // ------------------------------------------------------------- js bridge

    private class NativeBridge {

        /** Single buzz. Amplitude 1..255, ignored on pre-Oreo vibrators. */
        @JavascriptInterface
        public void vibrate(int ms, int amplitude) {
            if (vibrator == null || !vibrator.hasVibrator() || ms <= 0) return;
            int dur = Math.min(ms, 4000);
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    int amp = Math.max(1, Math.min(255, amplitude));
                    if (!vibrator.hasAmplitudeControl()) amp = VibrationEffect.DEFAULT_AMPLITUDE;
                    vibrator.vibrate(VibrationEffect.createOneShot(dur, amp));
                } else {
                    legacyVibrate(dur);
                }
            } catch (Throwable ignored) {
            }
        }

        /** Comma-separated off/on millisecond pairs, e.g. "0,60,40,300". */
        @JavascriptInterface
        public void vibratePattern(String csv) {
            if (vibrator == null || !vibrator.hasVibrator() || csv == null) return;
            try {
                String[] parts = csv.split(",");
                long[] timings = new long[parts.length];
                long total = 0;
                for (int i = 0; i < parts.length; i++) {
                    timings[i] = Math.max(0, Math.min(4000, Long.parseLong(parts[i].trim())));
                    total += timings[i];
                }
                if (total <= 0) return;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createWaveform(timings, -1));
                } else {
                    legacyPattern(timings);
                }
            } catch (Throwable ignored) {
            }
        }

        @JavascriptInterface
        public void cancelVibration() {
            stopVibration();
        }

        @JavascriptInterface
        public boolean hasVibrator() {
            return vibrator != null && vibrator.hasVibrator();
        }

        @JavascriptInterface
        public void quit() {
            runOnUiThread(MainActivity.this::finish);
        }
    }

    @SuppressWarnings("deprecation")
    private void legacyVibrate(int ms) {
        vibrator.vibrate(ms);
    }

    @SuppressWarnings("deprecation")
    private void legacyPattern(long[] timings) {
        vibrator.vibrate(timings, -1);
    }

    // ------------------------------------------------------- local asset server

    /**
     * Serves assets/game/** over https://appassets.androidplatform.net/. Anything that is
     * not that host is refused outright: the game is entirely offline and has no business
     * reaching the network.
     */
    private static class AssetWebViewClient extends WebViewClient {

        private final AssetManager assets;

        AssetWebViewClient(AssetManager assets) {
            this.assets = assets;
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            return !ASSET_HOST.equals(request.getUrl().getHost());
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            Uri uri = request.getUrl();
            if (!ASSET_HOST.equals(uri.getHost())) return deny();

            String path = uri.getPath();
            if (path == null || path.equals("/")) path = "/index.html";

            // Reject traversal outright rather than trying to normalise it away.
            if (path.contains("..")) return deny();

            String assetPath = ASSET_ROOT + path.substring(1);
            try {
                Map<String, String> headers = new HashMap<>();
                headers.put("Cache-Control", "no-store");

                InputStream in;
                long length = -1;
                // The media stack will try a Range request for <audio> sources.
                // An interceptor cannot answer one, so declare the resource
                // non-seekable and give an exact length — that makes the player
                // fetch it whole instead of waiting on a 206 that never comes.
                try {
                    AssetFileDescriptor fd = assets.openFd(assetPath);
                    length = fd.getLength();
                    in = fd.createInputStream();
                } catch (IOException notUncompressed) {
                    // Compressed assets have no file descriptor; fall back to
                    // the plain stream and let the length stay unknown.
                    in = assets.open(assetPath);
                }
                headers.put("Accept-Ranges", "none");
                if (length >= 0) headers.put("Content-Length", Long.toString(length));

                return new WebResourceResponse(
                        mimeOf(assetPath), "UTF-8", 200, "OK", headers, in);
            } catch (IOException e) {
                return new WebResourceResponse("text/plain", "UTF-8", 404, "Not Found",
                        Collections.emptyMap(), null);
            }
        }

        private WebResourceResponse deny() {
            return new WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden",
                    Collections.emptyMap(), null);
        }

        private static String mimeOf(String path) {
            String p = path.toLowerCase();
            if (p.endsWith(".html")) return "text/html";
            if (p.endsWith(".js")) return "application/javascript";
            if (p.endsWith(".css")) return "text/css";
            if (p.endsWith(".ogg")) return "audio/ogg";
            if (p.endsWith(".json")) return "application/json";
            if (p.endsWith(".png")) return "image/png";
            if (p.endsWith(".svg")) return "image/svg+xml";
            if (p.endsWith(".woff2")) return "font/woff2";
            return "application/octet-stream";
        }
    }
}
