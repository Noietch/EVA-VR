package org.eva.pico.input;

import android.app.NativeActivity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.content.res.AssetManager;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.InputType;
import android.util.Log;
import android.graphics.Color;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
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
    private boolean configShown;
    private final Runnable reconnect = this::connect;
    private final Runnable testPulse = new Runnable() {
        @Override public void run() {
            if (testMode && resumed) {
                enqueue(0, 1.0, 300);
                enqueue(1, 1.0, 300);
            }
        }
    };

    @Override public void onCreate(Bundle state) {
        endpoint = getIntent().getStringExtra("server_url");
        testMode = getIntent().getBooleanExtra("haptic_test", false);
        if (endpoint == null || endpoint.trim().isEmpty()) {
            endpoint = getSharedPreferences("eva_vr", MODE_PRIVATE)
                .getString("server_url", null);
        }
        super.onCreate(state);
        setNativeAssetManager(getAssets());
        if (!testMode && getIntent().getStringExtra("server_url") == null) {
            handler.post(this::showConnectionDialog);
        }
    }
    @Override protected void onResume() {
        super.onResume(); resumed = true;
        if (testMode) handler.postDelayed(testPulse, 500);
        if (endpoint != null && !endpoint.trim().isEmpty()) connect();
        else if (!testMode) showConnectionDialog();
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
                            double intensity = msg.optDouble("intensity", 0);
                            double duration = msg.optDouble("duration_ms", 0);
                            if (!hand.equals("left") && !hand.equals("right")) return;
                            if (!Double.isFinite(intensity) || !Double.isFinite(duration) || intensity <= 0 || duration <= 0) return;
                            enqueue(hand.equals("left") ? 0 : 1,
                                intensity, duration);
                        } else if ("event_ack".equals(type)) {
                            Log.i(TAG, "event_ack accepted=" + msg.optBoolean("accepted"));
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

    private void showConnectionDialog() {
        if (configShown || isFinishing() || testMode) return;
        configShown = true;
        final EditText address = new EditText(this);
        address.setSingleLine(true);
        address.setHint("192.168.1.20:43876");
        address.setText(endpoint == null ? "" : endpoint
            .replace("ws://", "").replace("wss://", "")
            .replace("/ws?token=eva", ""));
        address.setSelectAllOnFocus(false);
        address.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, 0, padding, 0);
        TextView help = new TextView(this);
        help.setText("输入运行 EVA-CLIENT 的 Linux 地址。\nToken 固定为 eva，端口默认 43876。\n例如：192.168.1.20:43876");
        help.setTextColor(Color.WHITE);
        help.setPadding(0, 0, 0, padding / 2);
        content.addView(help, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        content.addView(address, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("EVA-VR 连接设置")
            .setView(content)
            .setNegativeButton("退出", (d, which) -> finish())
            .setPositiveButton("连接", null)
            .create();
        dialog.setOnShowListener(d -> {
            dialog.getButton(DialogInterface.BUTTON_POSITIVE).setOnClickListener(v -> {
                String value = address.getText().toString().trim();
                String normalized = normalizeEndpoint(value);
                if (normalized == null) {
                    address.setError("请输入 Linux IP 或 ws:// 地址");
                    return;
                }
                endpoint = normalized;
                getSharedPreferences("eva_vr", MODE_PRIVATE).edit()
                    .putString("server_url", endpoint).apply();
                dialog.dismiss();
                connect();
            });
            address.requestFocus();
            dialog.getWindow().setSoftInputMode(
                android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        });
        dialog.setOnDismissListener(d -> configShown = false);
        dialog.show();
    }

    private String normalizeEndpoint(String value) {
        if (value == null || value.trim().isEmpty()) return null;
        String input = value.trim();
        if (input.startsWith("ws://") || input.startsWith("wss://")) {
            try {
                HttpUrl parsed = HttpUrl.parse(input);
                if (parsed == null || parsed.host().isEmpty()) return null;
                return parsed.newBuilder().encodedPath("/ws")
                    .setQueryParameter("token", "eva").build().toString();
            } catch (IllegalArgumentException error) { return null; }
        }
        if (!input.contains(":")) input += ":43876";
        HttpUrl parsed = HttpUrl.parse("ws://" + input);
        if (parsed == null || parsed.host().isEmpty()) return null;
        return parsed.newBuilder().encodedPath("/ws")
            .setQueryParameter("token", "eva").build().toString();
    }
}
