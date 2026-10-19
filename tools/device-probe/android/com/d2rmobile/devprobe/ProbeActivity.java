package com.d2rmobile.devprobe;

import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public final class ProbeActivity extends Activity {
    static { System.loadLibrary("deviceprobe"); }
    private static native String nativeProbe(String capturePath);
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text = new TextView(this);
        text.setTextSize(15);
        text.setPadding(24, 40, 24, 24);
        text.setText("D2R device probe\nTesting ARM64 execution and Vulkan compute...");
        ScrollView scroll = new ScrollView(this);
        scroll.addView(text);
        setContentView(scroll);
        new Thread(() -> {
            String result;
            try {
                String capturePath = getIntent().getBooleanExtra("renderdoc_capture", false)
                    ? new File(getFilesDir(), "renderdoc-probe").getAbsolutePath() : null;
                result = nativeProbe(capturePath);
                File temporary = new File(getFilesDir(), "report.tmp");
                try (FileOutputStream file = new FileOutputStream(temporary)) {
                    file.write(result.getBytes(StandardCharsets.UTF_8));
                }
                if (!temporary.renameTo(new File(getFilesDir(), "report.json")))
                    throw new java.io.IOException("Could not save report");
            } catch (Throwable error) {
                result = "Probe failed: " + error;
            }
            final String display = result;
            runOnUiThread(() -> text.setText("D2R device probe\n\n" + display +
                "\nThis tests basic runtime and Vulkan functions. It does not measure D2R compatibility or frame rate."));
        }, "device-probe").start();
    }
}
