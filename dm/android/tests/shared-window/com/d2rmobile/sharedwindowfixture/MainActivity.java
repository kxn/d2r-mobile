package com.d2rmobile.sharedwindowfixture;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;

// Dedicated owned package. Never invokes the normal game's MainActivity/start.
public final class MainActivity extends Activity {
    static { System.loadLibrary("dmhost_android"); }
    private static native int nativeRun(String directory, String libraryDirectory);
    private boolean started;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text = new TextView(this);
        text.setText("D2R owned shared-window GPU fixture");
        setContentView(text);
        if (state != null || started) { text.setText("Cold fixture restart rejected"); return; }
        started = true;
        String requested = getIntent().getStringExtra("fixture_directory");
        try {
            if (requested == null) throw new IllegalArgumentException("Missing fixture directory");
            final String directory = new File(requested).getCanonicalPath();
            String files = getFilesDir().getCanonicalPath() + File.separator;
            if (!directory.startsWith(files) || !new File(directory).isDirectory())
                throw new IllegalArgumentException("Fixture must be prepared inside this app's files");
            final String libraries = getApplicationInfo().nativeLibraryDir;
            new Thread(() -> {
                int result = nativeRun(directory, libraries);
                runOnUiThread(() -> text.setText("Owned GPU fixture finished: " + result));
            }, "owned-vulkan-fixture").start();
        } catch (Exception error) { text.setText(error.toString()); }
    }
}
