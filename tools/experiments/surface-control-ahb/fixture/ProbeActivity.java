package org.kxn.scahb;

import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;

/** Independent presentation correctness fixture; contains no game or profiler. */
public final class ProbeActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("scahbprobe"); }
    private static native void render(Surface surface, String directory, String hooks,
                                      boolean queryOnly, int color);
    private static native void stop();
    private static native void prepare();
    private static native void color(int value);
    private Thread worker;
    private SurfaceHolder holder;
    private boolean resumed;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            File driver = new File(getFilesDir(), "libvulkan_freedreno.so");
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            try (InputStream in = getAssets().open("driver.so");
                 FileOutputStream out = new FileOutputStream(driver)) {
                byte[] block = new byte[65536]; int n;
                while ((n = in.read(block)) != -1) { out.write(block, 0, n); hash.update(block, 0, n); }
            }
            StringBuilder actual = new StringBuilder();
            for (byte b : hash.digest()) actual.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            byte[] expected = new byte[64];
            try (InputStream in = getAssets().open("driver.sha256")) {
                int done = 0, n;
                while (done < 64 && (n = in.read(expected, done, 64-done)) > 0) done += n;
                if (done != 64 || !actual.toString().equals(new String(expected, "US-ASCII")))
                    throw new IllegalStateException("fixture driver identity mismatch");
            }
        } catch (Exception e) { throw new IllegalStateException(e); }
        SurfaceView view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        android.widget.FrameLayout frame = new android.widget.FrameLayout(this);
        frame.addView(view);
        android.widget.TextView label = new android.widget.TextView(this);
        label.setText("SurfaceControl AHB • API36 fixture • no game");
        label.setTextColor(android.graphics.Color.WHITE);
        label.setBackgroundColor(0x90000000);
        label.setPadding(20, 12, 20, 12);
        frame.addView(label);
        setContentView(frame);
    }
    private void start() {
        if (!resumed || holder == null || worker != null || holder.getSurfaceFrame().width() <= 0) return;
        final Surface surface = holder.getSurface();
        final boolean queryOnly = getIntent().getBooleanExtra("query_only", false);
        final int selected = getIntent().getIntExtra("color", 0);
        prepare();
        worker = new Thread(() -> render(surface, getFilesDir().getAbsolutePath(),
                getApplicationInfo().nativeLibraryDir, queryOnly, selected), "sc-ahb-functional");
        worker.start();
    }
    private void stopWorker() {
        if (worker == null) return;
        stop();
        try { worker.join(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        worker = null;
    }
    @Override public void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        if (intent.hasExtra("color")) {
            int selected = intent.getIntExtra("color", 0);
            getIntent().putExtra("color", selected);
            color(selected);
        }
    }
    @Override public void onResume() { super.onResume(); resumed = true; start(); }
    @Override public void onPause() { resumed = false; stopWorker(); super.onPause(); }
    @Override public void surfaceCreated(SurfaceHolder value) { holder = value; start(); }
    @Override public void surfaceChanged(SurfaceHolder value, int format, int width, int height) { stopWorker(); start(); }
    @Override public void surfaceDestroyed(SurfaceHolder value) { stopWorker(); holder = null; }
}
