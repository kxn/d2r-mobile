// Event-driven public Android APIs only; no polling or host bridge.
package com.d2rmobile.nativeport;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.PowerManager;
import java.util.Locale;

final class ThermalReadout {
    private final Context context;
    private final Runnable onChanged;
    private final PowerManager power;
    private PowerManager.OnThermalStatusChangedListener listener;
    private BroadcastReceiver receiver;
    private String[] displayed = {"电池：--"};
    private int systemStatus = -1;
    private boolean running;
    private int generation;
    ThermalReadout(Context context, Runnable onChanged) {
        this.context = context.getApplicationContext();
        this.onChanged = onChanged;
        power = (PowerManager)this.context.getSystemService(Context.POWER_SERVICE);
    }
    void start() {
        if (running) return;
        running = true;
        final int token = ++generation;
        if (power != null) {
            try { systemStatus = power.getCurrentThermalStatus(); }
            catch (RuntimeException unavailable) { systemStatus = -1; }
            PowerManager.OnThermalStatusChangedListener next = status -> {
                if (!running || generation != token) return;
                systemStatus = status;
                if (onChanged != null) onChanged.run();
            };
            try { power.addThermalStatusListener(context.getMainExecutor(), next); listener = next; }
            catch (RuntimeException unavailable) { listener = null; }
        }
        receiver = new BroadcastReceiver() {
            @Override public void onReceive(Context source, Intent intent) {
                if (running && generation == token) battery(intent);
            }
        };
        try { battery(context.registerReceiver(receiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED))); }
        catch (RuntimeException unavailable) { receiver = null; }
        if (onChanged != null) onChanged.run();
    }
    void stop() {
        if (!running) return;
        running = false;
        ++generation;
        if (listener != null) {
            try { power.removeThermalStatusListener(listener); }
            catch (RuntimeException unavailable) { /* Continue cleanup. */ }
            listener = null;
        }
        if (receiver != null) {
            try { context.unregisterReceiver(receiver); }
            catch (RuntimeException unavailable) { /* Already removed. */ }
            receiver = null;
        }
    }
    String[] lines() { return displayed; }
    int status() { return systemStatus; }
    boolean hot() { return systemStatus >= PowerManager.THERMAL_STATUS_SEVERE; }
    private void battery(Intent intent) {
        if (intent == null || !Intent.ACTION_BATTERY_CHANGED.equals(intent.getAction())) return;
        int tenths = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
        String next = tenths == Integer.MIN_VALUE ? "电池：--" :
            String.format(Locale.ROOT, "电池：%.1f°C", tenths / 10f);
        if (!next.equals(displayed[0])) {
            displayed = new String[]{next};
            if (onChanged != null) onChanged.run();
        }
    }
}
