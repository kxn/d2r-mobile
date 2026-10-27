package com.d2rmobile.texelformatprobe;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;
public final class ProbeActivity extends Activity {
  static { System.loadLibrary("texelformatprobe"); }
  private static boolean started;
  private static native int runProbe(String root,String hooks,int power,long chunks,long start,boolean quiet,boolean inject,boolean full);
  @Override public void onCreate(Bundle state) {
    super.onCreate(state);TextView text=new TextView(this);text.setText("独立格式验证；默认不运行");setContentView(text);
    if(!getIntent().getBooleanExtra("run_probe",false)||started)return;
    int power=getIntent().getIntExtra("chunk_pow",12);long chunks=getIntent().getLongExtra("chunks",1),start=getIntent().getLongExtra("start_chunk",0);
    String mode=getIntent().getStringExtra("conversion");if(mode==null)mode="raw";
    boolean quiet="quiet".equals(mode),inject=getIntent().getBooleanExtra("inject_error",false),full=getIntent().getBooleanExtra("full",false);
    if((!quiet&&!"raw".equals(mode))||power<7||power>20||chunks<1||start<0||(full&&start!=0))throw new IllegalArgumentException("Explicit valid cold probe options required");
    File root=new File(getFilesDir(),"runtime");root.mkdirs();new File(root,"shader-cache").mkdirs();started=true;
    new Thread(()->{int rc=runProbe(root.getAbsolutePath(),getApplicationInfo().nativeLibraryDir,power,chunks,start,quiet,inject,full);runOnUiThread(()->text.setText("独立验证结束，结果码 "+rc+"；详见 probe-output.log"));},"owned-texel-probe").start();
  }
}
