package io.sameboy.android;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;


/** Link-cable connect screen: Host (listen on 1989) or Join (peer IP), with a live status
 *  line polling NativeBridge.nativeLinkStatus. TCP over the local network. */
public final class LinkActivity extends DpadActivity {
    public static final String EXTRA_CTX = "io.sameboy.ctx";
    private static final int PORT = 1989;
    private long ctx;
    private LinkUi.Model model;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final String[] names = { "Idle", "Listening", "Connecting", "Connected", "Error" };
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (dead()) { finish(); return; }
            int st = NativeBridge.nativeLinkStatus(ctx);
            model.setStatus(names[st >= 0 && st < names.length ? st : 0]);
            handler.postDelayed(this, 500);
        }
    };

    /** EmulatorActivity destroyed beneath us → ctx freed; using it would be a native UAF. */
    private boolean dead() { return ctx == 0 || ctx != EmulatorActivity.activeCtx; }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (b != null) { finish(); return; }          // process-death: stale ctx
        ctx = getIntent().getLongExtra(EXTRA_CTX, 0);
        if (ctx == 0 || ctx != EmulatorActivity.activeCtx) { finish(); return; }

        model = LinkUi.bind(this,
            "This device: " + localIp() + "  (port " + PORT + ")",
            new LinkUi.Callbacks() {
                @Override public void onHost() { if (!dead()) NativeBridge.nativeLinkListen(ctx, PORT); }
                @Override public void onJoin(String ip) { if (!dead()) NativeBridge.nativeLinkConnect(ctx, ip, PORT); }
                @Override public void onDisconnect() { if (!dead()) NativeBridge.nativeLinkDisconnect(ctx); }
                @Override public void onBack() { finish(); }
            });
    }

    @Override protected void onResume() { super.onResume(); handler.post(poll); }
    @Override protected void onPause() { super.onPause(); handler.removeCallbacks(poll); }

    private String localIp() {
        try {
            java.util.Enumeration<java.net.NetworkInterface> ifs = java.net.NetworkInterface.getNetworkInterfaces();
            while (ifs.hasMoreElements()) {
                java.net.NetworkInterface nif = ifs.nextElement();
                if (!nif.isUp() || nif.isLoopback()) continue;
                java.util.Enumeration<java.net.InetAddress> addrs = nif.getInetAddresses();
                while (addrs.hasMoreElements()) {
                    java.net.InetAddress a = addrs.nextElement();
                    if (!a.isLoopbackAddress() && a instanceof java.net.Inet4Address) return a.getHostAddress();
                }
            }
        } catch (Exception ignored) {}
        return "?";
    }
}
