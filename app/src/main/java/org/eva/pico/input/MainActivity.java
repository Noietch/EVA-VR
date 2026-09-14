package org.eva.pico.input;

import android.app.NativeActivity;
import android.os.Bundle;
import android.content.res.AssetManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import okhttp3.*;
import org.json.JSONObject;

public class MainActivity extends NativeActivity {
    static { System.loadLibrary("openxr_loader"); System.loadLibrary("eva_pico"); }
    public native void setNativeAssetManager(AssetManager assetManager);
    private static final String TAG = "EVA-VR";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ArrayBlockingQueue<double[]> haptics = new ArrayBlockingQueue<>(16);
    private final OkHttpClient client = new OkHttpClient.Builder()
        .pingInterval(5, TimeUnit.SECONDS).build();
    private volatile WebSocket socket;
    private volatile boolean connected, resumed, focused = true;
    private String endpoint;
    private boolean testMode;
    private final Runnable reconnect = this::connect;
    private final Runnable testPulse = new Runnable() {
        @Override public void run() {
            if (testMode && resumed) {
                enqueue(0, 1.0, 300);
                enqueue(1, 1.0, 300);
                handler.postDelayed(this, 2500);
            }
        }
    };

    @Override public void onCreate(Bundle state) {
        endpoint = getIntent().getStringExtra("server_url");
        testMode = getIntent().getBooleanExtra("haptic_test", endpoint == null);
        super.onCreate(state);
        setNativeAssetManager(getAssets());
        if (testMode) handler.postDelayed(testPulse, 1200);
    }
    @Override protected void onResume() {
        super.onResume(); resumed = true; if (testMode) handler.postDelayed(testPulse, 500); connect();
    }
    @Override protected void onPause() {
        resumed = false; handler.removeCallbacks(testPulse); disconnect(); super.onPause();
    }
    @Override protected void onDestroy() {
        disconnect(); client.dispatcher().executorService().shutdown();
        client.connectionPool().evictAll(); super.onDestroy();
    }
    private synchronized void disconnect() {
        handler.removeCallbacks(reconnect);
        WebSocket old = socket; socket = null; connected = false;
        haptics.clear();
        if (old != null) old.cancel();
    }
    private synchronized void connect() {
        if (!resumed || socket != null || endpoint == null) return;
        try {
            Request request = new Request.Builder().url(endpoint).build();
            socket = client.newWebSocket(request, new WebSocketListener() {
                @Override public void onOpen(WebSocket ws, Response response) {
                    synchronized (MainActivity.this) {
                        if (socket != ws || !resumed) { ws.cancel(); return; }
                        connected = true;
                    }
                    Log.i(TAG, "WebSocket connected");
                }
                @Override public void onMessage(WebSocket ws, String text) {
                    if (socket != ws || !connected || !focused) return;
                    try {
                        JSONObject msg = new JSONObject(text);
                        String type = msg.optString("type");
                        if ("haptic".equals(type)) {
                            String hand = msg.optString("hand");
                            if (!hand.equals("left") && !hand.equals("right")) return;
                            enqueue(hand.equals("left") ? 0 : 1,
                                msg.optDouble("intensity", 0), msg.optDouble("duration_ms", 80));
                        } else if ("event_ack".equals(type)) {
                            boolean accepted = msg.optBoolean("accepted");
                            Log.i(TAG, "event_ack accepted=" + accepted);
                            for (int i = 0; i < 2; i++) enqueue(i, accepted ? .45 : 1, accepted ? 90 : 240);
                        }
                    } catch (Exception error) { Log.w(TAG, "Invalid server message"); }
                }
                @Override public void onClosing(WebSocket ws, int code, String reason) {
                    ws.close(code, reason);
                }
                @Override public void onClosed(WebSocket ws, int code, String reason) { lost(ws); }
                @Override public void onFailure(WebSocket ws, Throwable t, Response r) {
                    Log.w(TAG, "WebSocket failure: " + t.getClass().getSimpleName()); lost(ws);
                }
            });
        } catch (IllegalArgumentException error) { Log.e(TAG, "Invalid server_url"); }
    }
    private synchronized void lost(WebSocket ws) {
        if (socket != ws) return;
        socket = null; connected = false; haptics.clear();
        handler.removeCallbacks(reconnect);
        if (resumed) handler.postDelayed(reconnect, 1500);
    }
    private void enqueue(int hand, double intensity, double duration) {
        if (!Double.isFinite(intensity) || !Double.isFinite(duration)) return;
        double[] command = { hand, Math.max(0, Math.min(1, intensity)),
            Math.max(1, Math.min(1000, duration)), SystemClock.elapsedRealtime() };
        if (!haptics.offer(command)) { haptics.poll(); haptics.offer(command); }
    }
    // Called only by the OpenXR thread. The SDK is never called from a network callback.
    public double[] pollHaptic() {
        double[] command;
        while ((command = haptics.poll()) != null) {
            if (SystemClock.elapsedRealtime() - command[3] < 500) return command;
        }
        return null;
    }
    public void scanFile(String path) { Log.i(TAG, "scanFile " + path); }
    public void setFocused(boolean value) {
        focused = value;
        if (!value) haptics.clear();
    }
    public boolean isTestMode() { return testMode; }
    public boolean isConnected() { return connected; }
    public void sendFrame(String frame) {
        WebSocket ws = socket;
        if (resumed && connected && ws != null && ws.queueSize() < 65536) ws.send(frame);
    }
}
