package com.d2rmobile.nativeport;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.util.TypedValue;
import android.widget.*;
import java.util.Locale;

/** Same-Activity view: opening it never pauses the game or creates a Window. */
final class DebugPanel extends FrameLayout {
    static final class Settings {
        boolean thermalCap = true, thermalPause = true;
        int manualCap, scalePercent;
    }
    interface Actions {
        void close();
        void thermalCap(boolean enabled);
        void thermalPause(boolean enabled);
        void manualCap(int fps);
        void scalePercent(int percent);
    }
    private final TextView details;
    private final ScrollView scroll;
    private final Switch cap, pause;
    private final Spinner manual, renderScale;
    private final Settings settings;
    private final Actions actions;
    private boolean syncing;
    private String lastDetails, previousBattery;
    private final float[] previousState = new float[9];
    private int previousFpsTenths, previousThermal;
    private boolean havePrevious, previousCap, previousPause;
    private int previousManual;
    private static final int[] LIMITS = {0, 20, 30, 40, 60};
    private static final int[] SCALES = {0, 50, 60, 65, 70, 75, 85, 100};
    DebugPanel(Context context, Settings settings, Actions actions) {
        super(context); this.settings = settings; this.actions = actions;
        setBackgroundColor(0x33000000);
        setClickable(true);
        setOnClickListener(v -> actions.close());
        scroll = new ScrollView(context);
        LinearLayout body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(10), dp(6), dp(10), dp(6));
        body.setBackgroundColor(0xf2222222);
        body.setClickable(true); // Blank panel space also consumes the gesture.
        scroll.addView(body);
        LayoutParams position = new LayoutParams(dp(360), LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.LEFT);
        position.leftMargin = dp(8); position.topMargin = dp(8); position.bottomMargin = dp(8);
        addView(scroll, position);
        LinearLayout header = new LinearLayout(context);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text(context, "调试");
        header.addView(title, new LinearLayout.LayoutParams(0, dp(36), 1));
        Button reset = button(context, "默认");
        header.addView(reset, new LinearLayout.LayoutParams(dp(68), dp(36)));
        Button close = button(context, "收起");
        close.setOnClickListener(v -> actions.close());
        header.addView(close, new LinearLayout.LayoutParams(dp(68), dp(36)));
        body.addView(header);
        details = text(context, "等待出图状态"); body.addView(details);
        cap = toggle(context, "热限帧");
        pause = toggle(context, "过热暂停");
        body.addView(cap, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(36)));
        body.addView(pause, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(36)));
        LinearLayout capRow = new LinearLayout(context); capRow.setGravity(Gravity.CENTER_VERTICAL);
        capRow.addView(text(context, "帧率上限"), new LinearLayout.LayoutParams(0, dp(36), 1));
        manual = new Spinner(context);
        manual.setAdapter(options(context, new String[]{"跟随", "20 FPS", "30 FPS", "40 FPS", "60 FPS"}));
        capRow.addView(manual, new LinearLayout.LayoutParams(dp(130), dp(36)));
        body.addView(capRow);
        LinearLayout scaleRow = new LinearLayout(context); scaleRow.setGravity(Gravity.CENTER_VERTICAL);
        scaleRow.addView(text(context, "3D比例"), new LinearLayout.LayoutParams(0, dp(36), 1));
        renderScale = new Spinner(context);
        renderScale.setAdapter(options(context, new String[]{"自动", "固定50%", "固定60%", "固定65%", "固定70%", "固定75%（iOS）", "固定85%", "固定100%"}));
        scaleRow.addView(renderScale, new LinearLayout.LayoutParams(dp(175), dp(36)));
        body.addView(scaleRow);
        sync();
        cap.setOnCheckedChangeListener((v, checked) -> { if (!syncing) actions.thermalCap(checked); });
        pause.setOnCheckedChangeListener((v, checked) -> { if (!syncing) actions.thermalPause(checked); });
        manual.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int index, long id) {
                if (!syncing && settings.manualCap != LIMITS[index]) actions.manualCap(LIMITS[index]);
            }
        });
        renderScale.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int index, long id) {
                if (!syncing && settings.scalePercent != SCALES[index]) actions.scalePercent(SCALES[index]);
            }
        });
        reset.setOnClickListener(v -> {
            actions.thermalCap(true); actions.thermalPause(true); actions.manualCap(0); actions.scalePercent(0); sync();
        });
        setVisibility(GONE);
    }
    private ArrayAdapter<String> options(Context context, String[] labels) {
        return new ArrayAdapter<String>(context, android.R.layout.simple_spinner_dropdown_item, labels) {
            private View compact(View v) {
                TextView label = (TextView)v;
                label.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12);
                label.setMinHeight(dp(36)); label.setMinimumHeight(dp(36)); label.setPadding(dp(8), 0, dp(8), 0);
                return v;
            }
            @Override public View getView(int position, View convert, ViewGroup parent) {
                return compact(super.getView(position, convert, parent));
            }
            @Override public View getDropDownView(int position, View convert, ViewGroup parent) {
                return compact(super.getDropDownView(position, convert, parent));
            }
        };
    }
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        LayoutParams position = (LayoutParams)scroll.getLayoutParams();
        position.width = Math.max(1, Math.min(dp(360), w - dp(16)));
        // WRAP_CONTENT is measured within the actual parent height minus margins.
        // Scroll remains only as a fallback for unusually small windows.
        scroll.setLayoutParams(position);
    }
    private TextView text(Context context, String value) {
        TextView v = new TextView(context); v.setText(value); v.setTextColor(Color.WHITE);
        v.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12);
        v.setIncludeFontPadding(false); v.setGravity(Gravity.CENTER_VERTICAL);
        v.setPadding(0, dp(2), 0, dp(2)); return v;
    }
    private Button button(Context context, String value) {
        Button v = new Button(context); v.setText(value);
        v.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12);
        v.setMinHeight(0); v.setMinimumHeight(0); v.setMinWidth(0); v.setMinimumWidth(0);
        v.setPadding(dp(6), 0, dp(6), 0); return v;
    }
    private Switch toggle(Context context, String value) {
        Switch v = new Switch(context); v.setText(value); v.setTextColor(Color.WHITE);
        v.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12);
        v.setMinHeight(dp(36)); v.setMinimumHeight(dp(36));
        v.setPadding(0, 0, 0, 0); return v;
    }
    void sync() {
        syncing = true;
        cap.setChecked(settings.thermalCap); pause.setChecked(settings.thermalPause);
        for (int i = 0; i < LIMITS.length; ++i) if (LIMITS[i] == settings.manualCap) manual.setSelection(i);
        for (int i = 0; i < SCALES.length; ++i) if (SCALES[i] == settings.scalePercent) renderScale.setSelection(i);
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
        String status = thermal >= 0 && thermal < names.length ? names[thermal] : "--";
        String value = String.format(Locale.ROOT,
                "FPS %s | %s | 温控%s\n目标 %.0f→%.0f | 手动%s / 热%s\n输出 %.0f×%.0f\n3D %.0f%%",
                fps > 0 ? String.format(Locale.ROOT, "%.1f", fps) : "--", battery.replace("：", ""), status,
                state[0], state[1], limit(state[3]), limit(state[2]), state[4], state[5], state[8]*100);
        if (state[4] != state[6] || state[5] != state[7])
            value += String.format(Locale.ROOT, "\nSurface %.0f×%.0f", state[6], state[7]);
        if (!value.equals(lastDetails)) { lastDetails = value; details.setText(value); }
    }
    private static String limit(float n) { return n > 0 ? String.format(Locale.ROOT, "%.0f", n) : "跟随"; }
}
