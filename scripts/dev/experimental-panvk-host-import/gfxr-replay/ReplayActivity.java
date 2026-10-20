package com.d2rmobile.panvkgfxrfixture;
import android.app.NativeActivity;
import android.os.Bundle;
public final class ReplayActivity extends NativeActivity {
    static { System.loadLibrary("panvk_replay_bridge"); }
    private static native void nativeConfigure(String files,String libs,String token,boolean rebindBda);
    @Override public void onCreate(Bundle state) {
        String token=getIntent().getStringExtra("fixture_token");
        if(token==null || !token.matches("[0-9a-f]{32}"))throw new IllegalArgumentException("cold fixture token required");
        boolean rebindBda=getIntent().getBooleanExtra("tool_rebind_bda",false);
        if(rebindBda) {
            // Deliberately accept only this fixture's fixed official-rebind command,
            // not a second parser or general feature-override interface.
            String files=getFilesDir().getAbsolutePath();
            String expected="--sync --serialize-render-passes --validate --log-file "+files+"/run-"+token+"/gfxr.log --memory-translation rebind "+files+"/capture.gfxr";
            if(!expected.equals(getIntent().getStringExtra("args")))throw new IllegalArgumentException("tool BDA requires fixed official rebind command");
        }
        nativeConfigure(getFilesDir().getAbsolutePath(),getApplicationInfo().nativeLibraryDir,token,rebindBda);
        super.onCreate(state);
    }
}
