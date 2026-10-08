package io.sameboy.android;

import android.content.Context;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

class EmulatorSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    interface Listener {
        void onSurfaceReady(android.view.Surface surface);
        void onSurfaceGone();
    }
    private final Listener listener;

    EmulatorSurfaceView(Context ctx, Listener l) {
        super(ctx);
        this.listener = l;
        getHolder().addCallback(this);
    }

    @Override public void surfaceCreated(SurfaceHolder h) { listener.onSurfaceReady(h.getSurface()); }
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) {}
    @Override public void surfaceDestroyed(SurfaceHolder h) { listener.onSurfaceGone(); }
}
