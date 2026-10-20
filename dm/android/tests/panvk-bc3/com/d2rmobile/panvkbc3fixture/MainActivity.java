package com.d2rmobile.panvkbc3fixture;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;
// Only this independent package starts the bounded functional test; no game Activity.
public final class MainActivity extends Activity {
    static { System.loadLibrary("dmhost_android"); }
    private static native int nativeRun(String directory, String libraries);
    private boolean started;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        final TextView text=new TextView(this);setContentView(text);
        if(state!=null || started){text.setText("Cold restart rejected");return;}
        started=true;
        try {
            String requested=getIntent().getStringExtra("fixture_directory");
            if(requested==null)throw new IllegalArgumentException("Missing fixture directory");
            final String root=new File(requested).getCanonicalPath();
            if(!root.startsWith(getFilesDir().getCanonicalPath()+File.separator) || !new File(root).isDirectory())throw new IllegalArgumentException("Owned app files required");
            final String libraries=getApplicationInfo().nativeLibraryDir;
            new Thread(()->{int rc=nativeRun(root,libraries);runOnUiThread(()->text.setText("BC3 fixture finished: "+rc));},"owned-BC3-fixture").start();
        }catch(Exception error){text.setText(error.toString());}
    }
}
