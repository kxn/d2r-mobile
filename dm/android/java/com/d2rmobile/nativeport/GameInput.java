// Android 按键与摇杆交给原 dm_pad_set。
// Android keys and sticks feed original dm_pad_set.
package com.d2rmobile.nativeport;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
final class GameInput {
    private static native boolean nativeKey(int device, int code, boolean down);
    private static native void nativeAxes(int device, float lx, float ly, float rx, float ry, float hatX, float hatY, float lt, float rt);
    private static native void nativeReset();
    private static native void nativeRemoved(int device);
    private static InputManager manager;
    private static final InputManager.InputDeviceListener devices = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int id) {}
        @Override public void onInputDeviceRemoved(int id) { nativeRemoved(id); }
        @Override public void onInputDeviceChanged(int id) { nativeRemoved(id); }
    };
    static void initialize(Context context) {
        if (manager != null) return;
        manager=(InputManager)context.getApplicationContext().getSystemService(Context.INPUT_SERVICE);
        if(manager!=null)manager.registerInputDeviceListener(devices,new Handler(Looper.getMainLooper()));
    }
    static void shutdown() {
        if(manager!=null){manager.unregisterInputDeviceListener(devices);manager=null;}
        nativeReset();
    }
    static boolean key(KeyEvent e) {
        if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP) return false;
        return nativeKey(e.getDeviceId(),e.getKeyCode(), e.getAction() == KeyEvent.ACTION_DOWN);
    }
    private static float axis(MotionEvent e, int axis) {
        float value=e.getAxisValue(axis);
        // Keep raw normalized travel: upstream's .25/.15 stick hysteresis is
        // applied after merging devices, rather than rescaling each event.
        return Float.isNaN(value)||Float.isInfinite(value)?0:Math.max(-1,Math.min(1,value));
    }
    private static int rightAxis(MotionEvent e, int primary, int fallback) {
        InputDevice device=e.getDevice();
        return device!=null && device.getMotionRange(primary,e.getSource())!=null?primary:fallback;
    }
    static boolean motion(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) return false;
        if(e.getActionMasked()==MotionEvent.ACTION_CANCEL){nativeRemoved(e.getDeviceId());return true;}
        nativeAxes(e.getDeviceId(),axis(e, MotionEvent.AXIS_X), -axis(e, MotionEvent.AXIS_Y),
                   axis(e,rightAxis(e,MotionEvent.AXIS_Z,MotionEvent.AXIS_RX)), -axis(e,rightAxis(e,MotionEvent.AXIS_RZ,MotionEvent.AXIS_RY)),
                   e.getAxisValue(MotionEvent.AXIS_HAT_X), e.getAxisValue(MotionEvent.AXIS_HAT_Y),
                   Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE)),
                   Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS)));
        return true;
    }
    static void reset() { nativeReset(); }
}
