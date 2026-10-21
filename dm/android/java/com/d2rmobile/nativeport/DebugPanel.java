package com.d2rmobile.nativeport;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.*;
import java.util.Locale;

/** Same-Activity view: opening it never pauses the game or creates a Window. */
final class DebugPanel extends FrameLayout {
    static final class Settings {
        boolean thermalCap = true, thermalPause = true;
        int manualCap;
    }
    interface Actions {
        void close();
        void thermalCap(boolean enabled);
        void thermalPause(boolean enabled);
        void manualCap(int fps);
    }
    private final TextView details;
    private final Switch cap, pause;
    private final Spinner manual;
    private final Settings settings;
    private final Actions actions;
    private boolean syncing;
    private String lastDetails, previousBattery;
    private final float[] previousState = new float[9];
    private int previousFpsTenths, previousThermal;
    private boolean havePrevious, previousCap, previousPause;
    private int previousManual;
    private static final int[] LIMITS = {0, 20, 30, 40, 60};
    DebugPanel(Context context, Settings settings, Actions actions) {
        super(context); this.settings = settings; this.actions = actions;
        setBackgroundColor(0x33000000);
        setClickable(true);
        setOnClickListener(v -> actions.close());
        ScrollView scroll = new ScrollView(context);
        LinearLayout body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(16), dp(10), dp(16), dp(12));
        body.setBackgroundColor(0xf2222222);
        body.setClickable(true); // Blank panel space also consumes the gesture.
        scroll.addView(body);
        LayoutParams position = new LayoutParams(dp(360), LayoutParams.MATCH_PARENT, Gravity.TOP | Gravity.LEFT);
        position.leftMargin = dp(8); position.topMargin = dp(56); position.bottomMargin = dp(8);
        addView(scroll, position);
        Button close = new Button(context); close.setText("调试信息 · 收起");
        close.setOnClickListener(v -> actions.close()); body.addView(close);
        details = text(context, "等待出图状态"); body.addView(details);
        cap = new Switch(context); cap.setText("应用热限帧 / 热降分辨率"); cap.setTextColor(Color.WHITE);
        pause = new Switch(context); pause.setText("应用过热自动存档暂停"); pause.setTextColor(Color.WHITE);
        body.addView(cap); body.addView(pause);
        body.addView(text(context, "手动应用帧率上限（不修改游戏设置）"));
        manual = new Spinner(context);
        manual.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item,
                new String[]{"跟随原游戏", "20 FPS", "30 FPS", "40 FPS", "60 FPS"}));
        body.addView(manual);
        Button reset = new Button(context); reset.setText("恢复应用默认"); body.addView(reset);
        body.addView(text(context, "原游戏通常请求 60 FPS（省电设置为 30）。上限只减少应用出图；显示刷新率、系统温控和系统强制退出不受此面板控制。设置只保留在本次应用进程。"));
        sync();
        cap.setOnCheckedChangeListener((v, checked) -> { if (!syncing) actions.thermalCap(checked); });
        pause.setOnCheckedChangeListener((v, checked) -> { if (!syncing) actions.thermalPause(checked); });
        manual.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int index, long id) {
                if (!syncing && settings.manualCap != LIMITS[index]) actions.manualCap(LIMITS[index]);
            }
        });
        reset.setOnClickListener(v -> {
            actions.thermalCap(true); actions.thermalPause(true); actions.manualCap(0); sync();
        });
        setVisibility(GONE);
    }
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private static TextView text(Context context, String value) {
        TextView v = new TextView(context); v.setText(value); v.setTextColor(Color.WHITE);
        v.setTextSize(14); v.setPadding(0, 6, 0, 6); return v;
    }
    void sync() {
        syncing = true;
        cap.setChecked(settings.thermalCap); pause.setChecked(settings.thermalPause);
        for (int i = 0; i < LIMITS.length; ++i) if (LIMITS[i] == settings.manualCap) manual.setSelection(i);
        syncing = false;
    }
    void update(float fps, String battery, int thermal, float[] state) {
        if (getVisibility() != VISIBLE) return;
        int tenths = Math.round(fps * 10);
        boolean changed = !havePrevious || tenths != previousFpsTenths || thermal != previousThermal
                || !battery.equals(previousBattery) || previousCap != settings.thermalCap
                || previousPause != settings.thermalPause || previousManual != settings.manualCap;
        for (int i = 0; i < previousState.length; ++i)
            if (Float.floatToIntBits(state[i]) != Float.floatToIntBits(previousState[i])) changed = true;
        if (!changed) return;
        System.arraycopy(state, 0, previousState, 0, previousState.length);
        previousFpsTenths = tenths; previousThermal = thermal; previousBattery = battery;
        previousCap = settings.thermalCap; previousPause = settings.thermalPause;
        previousManual = settings.manualCap; havePrevious = true;
        String[] names = {"无", "轻度", "中度", "严重", "危急", "紧急", "关机"};
        String status = thermal >= 0 && thermal < names.length ? names[thermal] + " (" + thermal + ")" : "不可读取";
        String value = String.format(Locale.ROOT,
                "实测 FPS：%s\n%s · 系统热状态：%s\n游戏请求：%.0f · 有效目标：%.0f FPS\n应用上限：手动 %s / 热 %s\n游戏输出：%.0f × %.0f\nSurface：%.0f × %.0f\n实际 3D 比例：%.2f",
                fps > 0 ? String.format(Locale.ROOT, "%.1f", fps) : "--", battery, status,
                state[0], state[1], limit(state[3]), limit(state[2]),
                state[4], state[5], state[6], state[7], state[8]);
        if (!value.equals(lastDetails)) { lastDetails = value; details.setText(value); }
    }
    private static String limit(float n) { return n > 0 ? String.format(Locale.ROOT, "%.0f", n) : "跟随"; }
}
