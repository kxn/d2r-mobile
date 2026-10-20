package com.d2rmobile.panvkgfxrfixture;
import android.app.NativeActivity;
import android.os.Bundle;
public final class ReplayActivity extends NativeActivity {
    static { System.loadLibrary("panvk_replay_bridge"); }
    private static native void nativeConfigure(String files,String libs,String token);
    @Override public void onCreate(Bundle state) {
        String token=getIntent().getStringExtra("fixture_token");
        if(token==null || !token.matches("[0-9a-f]{32}"))throw new IllegalArgumentException("cold fixture token required");
        nativeConfigure(getFilesDir().getAbsolutePath(),getApplicationInfo().nativeLibraryDir,token);
        super.onCreate(state);
    }
}
