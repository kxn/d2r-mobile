package com.d2rmobile.nativeastcfilterfixture;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;
public final class MainActivity extends Activity {
 static { System.loadLibrary("native_astc_filter_matrix"); }
 private static native int nativeRun(String root,String libraries,int shape,boolean realResource);
 private boolean started;
 @Override public void onCreate(Bundle state) {
  super.onCreate(state);TextView text=new TextView(this);text.setText("Owned ASTC linear GPU byte oracle");setContentView(text);
  if(state!=null || started){text.setText("Cold restart rejected");return;}started=true;
  try {
   final String root=new File(getIntent().getStringExtra("fixture_directory")).getCanonicalPath();
   if(!root.startsWith(getFilesDir().getCanonicalPath()+File.separator)||!new File(root).isDirectory())throw new IllegalArgumentException("Owned app-private directory required");
   final String libraries=getApplicationInfo().nativeLibraryDir;
   final boolean realResource="resource-bake".equals(getIntent().getStringExtra("mode"));
   final int shape=getIntent().getIntExtra("shape",-1);if(shape < -1 || shape > 3)throw new IllegalArgumentException("Finite shape selector");
   new Thread(()->{int rc=nativeRun(root,libraries,shape,realResource);runOnUiThread(()->text.setText("GPU oracle returned "+rc));},"owned-astc-linear-fixture").start();
  }catch(Exception e){text.setText(e.toString());}
 }
}
