// Canvas 只画原 C 手势层给出的布局；触点不直接改游戏内存。
// Canvas renders the original C gesture layer's layout; touches never write
// game memory directly.
package com.d2rmobile.nativeport;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.graphics.RectF;
import java.io.IOException;
import java.io.InputStream;
import android.util.SparseIntArray;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.BlendMode;
import android.view.MotionEvent;
import android.view.View;
import android.view.Choreographer;
import android.view.WindowInsets;
import java.util.Locale;
final class TouchControls extends View {
    private static native void nativeSize(float width, float height);
    private static native void nativeTouch(int phase, int slot, float x, float y);
    private static native void nativeReset();
    private static native void nativeFrame(float[] output);
    private static native boolean nativeGhost(float[] output);
    private final float[] ghostRect = new float[4];
    private static native String[] nativeLabels();
    private final float[] layout = new float[164];
    private final float[] nextLayout = new float[164], nextGhost = new float[4];
    private final Choreographer choreographer = Choreographer.getInstance();
    private boolean ghostVisible, continuousRedraw;
    // 输入状态仍按显示节拍更新；相同画面复用 HWUI 显示列表，避免空闲重画。
    // Keep input/state ticks at display cadence; reuse HWUI display lists when
    // painted content is unchanged rather than redraw an idle overlay.
    private final Choreographer.FrameCallback frame = time -> pollFrame();
    private final Bitmap[] atlases = new Bitmap[9];
    private final Bitmap mapArt, menuArt, circleArt, joyArt;
    private final OriginalHud hud;
    private String[] labels;
    private int language = -1;
    private final Rect sourceRect = new Rect();
    private final RectF targetRect = new RectF();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint fpsPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF fpsBox = new RectF();
    private final float density;
    private final ThermalReadout thermal;
    private int fpsTenths = -1, fpsInsetLeft, fpsInsetTop;
    private String fpsText = "FPS --";
    private final SparseIntArray slots = new SparseIntArray();
    private float scale = 1;
    private boolean running;
    private final DebugTouchGate debugTouchGate = new DebugTouchGate();
    TouchControls(Context context, boolean debugPad) {
        super(context); setFocusable(false);
        density = context.getResources().getDisplayMetrics().density;
        thermal = new ThermalReadout(context, this::thermalChanged);
        fpsPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        fpsPaint.setTextSize(14 * density);
        // Production controls are the original touch layer, with no extra
        // virtual controller row or invisible debug hit regions.
        paint.setTypeface(Typeface.create("serif", Typeface.NORMAL));
        hud = new OriginalHud(context);
        for (int i = 0; i < atlases.length; i++)
            atlases[i] = art(context, "sil_dm_native_skill_" + i + ".png");
        circleArt = art(context, "dm_native_circle.png");
        joyArt = art(context, "dm_touch_joy.png");
        mapArt = art(context, "dm_native_map.png");
        menuArt = art(context, "dm_native_menu.png");
    }
    private static Bitmap art(Context context, String name) {
        try (InputStream stream = context.getAssets().open("touch/" + name)) {
            Bitmap bitmap = BitmapFactory.decodeStream(stream);
            if (bitmap == null) throw new IOException("Invalid PNG: " + name);
            return bitmap;
        } catch (IOException failure) { throw new IllegalStateException("Original touch art unavailable", failure); }
    }
    private boolean glyph(Canvas canvas, int button, float x, float y, float radius,
                          boolean held, boolean spent) {
        int n = 54 + button * 10, atlas = (int)layout[n+1];
        if (layout[n] != 2 || atlas < 0 || atlas >= atlases.length) return false;
        int start = n + (held ? 6 : 2), sx = (int)layout[start], sy = (int)layout[start+1];
        int w = (int)layout[start+2], h = (int)layout[start+3];
        if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || sx+w > atlases[atlas].getWidth() || sy+h > atlases[atlas].getHeight()) return false;
        sourceRect.set(sx, sy, sx+w, sy+h);
        // DmTouchOverlay: fit the unmodified atlas frame inside diameter*.64.
        float fit = radius*2*.64f/Math.max(w,h);
        targetRect.set(x-w*fit/2, y-h*fit/2, x+w*fit/2, y+h*fit/2);
        paint.setAlpha(Math.round((spent ? .55f : held ? 1 : .92f)*255));
        paint.setFilterBitmap(true); paint.setBlendMode(spent ? BlendMode.LUMINOSITY : BlendMode.SRC_OVER);
        canvas.drawBitmap(atlases[atlas], sourceRect, targetRect, paint);
        paint.setBlendMode(BlendMode.SRC_OVER); paint.setAlpha(255);
        return true;
    }
    private void bitmap(Canvas canvas, Bitmap image, float x, float y, float w, float h, float alpha) {
        targetRect.set(x,y,x+w,y+h); paint.setAlpha(Math.round(alpha*255));
        canvas.drawBitmap(image,null,targetRect,paint); paint.setAlpha(255);
    }
    private void color(float r, float g, float b, float a) {
        paint.setColor(Color.argb(Math.round(a*255),Math.round(r*255),Math.round(g*255),Math.round(b*255)));
    }
    private void shell(Canvas c, float x, float y, float r, boolean held) {
        float lw=Math.max(1.5f,r*.075f), fade=held?1:.6f;
        paint.setStyle(Paint.Style.FILL); color(.086f,.067f,.047f,held?.92f:.8f*fade);
        c.drawCircle(x,y,r,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(lw);
        color(.60f,.47f,.28f,held?1:.9f*fade); c.drawCircle(x,y,r-lw/2,paint);
        paint.setStrokeWidth(lw*.5f); color(.12f,.09f,.05f,.9f*fade);
        c.drawCircle(x,y,r-lw*1.3f,paint); paint.setStyle(Paint.Style.FILL);
    }
    private void caption(Canvas c, String text, float x, float top, float size, float alpha) {
        if (text==null || text.isEmpty()) return;
        paint.setStyle(Paint.Style.FILL); paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(size); color(.85f,.79f,.65f,alpha);
        c.drawText(text,x,top-paint.ascent(),paint);
    }
    private void scheduleFrame() {
        choreographer.removeFrameCallback(frame);
        if (running && isAttachedToWindow()) choreographer.postFrameCallback(frame);
    }
    private boolean refreshState() {
        nativeFrame(nextLayout);
        boolean ghost = nativeGhost(nextGhost), changed = ghost != ghostVisible;
        for (int i = 0; i < layout.length; i++) {
            // 129 是绑定快照年龄诊断值，不参与任何绘制；其余字段均保守比较。
            // Binding-snapshot age is diagnostic only, never painted. Compare
            // every other field conservatively, including icons and HUD values.
            if (i != 129 && Float.floatToIntBits(layout[i]) != Float.floatToIntBits(nextLayout[i])) changed = true;
        }
        if (ghost) for (int i = 0; i < 4; i++)
            if (Float.floatToIntBits(ghostRect[i]) != Float.floatToIntBits(nextGhost[i])) changed = true;
        System.arraycopy(nextLayout, 0, layout, 0, layout.length);
        int nextFps = Math.round(layout[158] * 10);
        if (nextFps != fpsTenths) {
            fpsTenths = nextFps;
            fpsText = nextFps > 0 ? String.format(Locale.ROOT, "FPS %.1f", nextFps / 10f) : "FPS --";
        }
        if (ghost) System.arraycopy(nextGhost, 0, ghostRect, 0, 4);
        ghostVisible = ghost;
        return changed;
    }
    private void pollFrame() {
        if (!running) return;
        if (refreshState() || continuousRedraw) invalidate();
        if (getContext() instanceof MainActivity)
            ((MainActivity)getContext()).updateDebugPanel(layout[158], thermal.lines()[0], thermal.status());
        scheduleFrame();
    }
    void continuousRedraw(boolean value) { continuousRedraw = value; invalidate(); }
    void running(boolean value) {
        running = value;
        if (value) { refreshState(); invalidate(); } else cancel();
        scheduleFrame();
    }
    void monitorThermal(boolean value) {
        if (value && isAttachedToWindow()) thermal.start(); else thermal.stop();
    }
    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (running) thermal.start();
        scheduleFrame();
    }
    @Override protected void onDetachedFromWindow() {
        thermal.stop();
        choreographer.removeFrameCallback(frame); cancel(); super.onDetachedFromWindow();
    }
    void cancel() { slots.clear(); nativeReset(); invalidate(); }
    private void cancelTouches() {
        // ACTION_CANCEL is not UP: no skill pulse, menu click or world tap.
        // Let the native bridge return a dragged inventory item as upstream does.
        for (int i=0;i<slots.size();++i) nativeTouch(3,slots.valueAt(i),0,0);
        slots.clear(); refreshState(); invalidate();
    }
    void blockForDebug(boolean blocked) {
        debugTouchGate.block(blocked);
        cancelTouches();
        nativeReset();
    }
    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        android.graphics.Insets safe = insets.getInsetsIgnoringVisibility(
                WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
        fpsInsetLeft = safe.left;
        fpsInsetTop = safe.top;
        invalidate();
        return super.onApplyWindowInsets(insets);
    }
    private void thermalChanged() {
        if (getContext() instanceof MainActivity)
            ((MainActivity)getContext()).deviceThermalStatus(thermal.status());
        postInvalidate();
    }
    private void drawFrameRate(Canvas c) {
        float x = fpsInsetLeft + 8*density, y = fpsInsetTop + 8*density;
        fpsPaint.setTextSize(14*density);
        float pad = 6*density, height = fpsPaint.descent() - fpsPaint.ascent();
        float width = fpsPaint.measureText(fpsText), lineHeight = 15*density;
        String[] temperatures = thermal.lines();
        fpsPaint.setTextSize(11*density);
        for (String line : temperatures) width = Math.max(width, fpsPaint.measureText(line));
        fpsBox.set(x, y, x + width + pad*2,
                y + height + pad*2 + temperatures.length*lineHeight);
        fpsPaint.setColor(thermal.hot() ? 0xbb661c12 : 0x99000000);
        c.drawRoundRect(fpsBox, 4*density, 4*density, fpsPaint);
        fpsPaint.setTextSize(14*density);
        fpsPaint.setColor(0xffeee7d7);
        c.drawText(fpsText, x + pad, y + pad - fpsPaint.ascent(), fpsPaint);
        fpsPaint.setTextSize(11*density);
        for (int i=0;i<temperatures.length;++i)
            c.drawText(temperatures[i], x+pad,
                    y+pad+height+lineHeight*(i+1)-fpsPaint.descent(), fpsPaint);
    }
    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        scale = Math.max(1, h) / 440f;
        nativeSize(w / scale, h / scale);
        cancel();
    }
    @Override protected void onDraw(Canvas c) {
        super.onDraw(c);
        if (!running) return;
        drawFrameRate(c);
        if (layout[157]!=0) return;
        c.save(); c.scale(scale, scale);
        if (ghostVisible) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(0xd1000000);
            c.drawRect(ghostRect[0], ghostRect[1], ghostRect[2], ghostRect[3], paint);
        }
        if (language != (int)layout[141]) { labels=nativeLabels(); language=(int)layout[141]; }
        float k=layout[50]/60;
        boolean navigation=layout[53]!=0;
        for(int i=0;i<12;++i) {
            float x=layout[i*4],y=layout[i*4+1],r=layout[i*4+2];
            if(r<=0 || (i>5 && i<=10)) continue;
            boolean held=layout[i*4+3]!=0, skill=!navigation && i<=5;
            boolean cancelMap=!navigation && i==11 && layout[146]!=0;
            if(cancelMap) { paint.setStyle(Paint.Style.FILL);color(.55f,.05f,.04f,.36f);c.drawRect(x-r,y-r,x+r,y+r,paint); }
            if(skill) shell(c,x,y,r,held);
            else bitmap(c,circleArt,x-r,y-r,r*2,r*2,held?.6f:.28f);
            if(skill) {
                boolean icon=layout[54+i*10]==2;
                boolean showCaption=layout[54+i*10]!=1 && !icon;
                if(!icon && (!showCaption || i==0)) {
                    float a=r*.30f;
                    paint.setStyle(Paint.Style.STROKE);paint.setStrokeCap(Paint.Cap.BUTT);
                    paint.setStrokeWidth(Math.max(1.5f,r*.085f));color(.60f,.47f,.28f,held?1:.9f);
                    c.drawLine(x-a,y,x+a,y,paint);c.drawLine(x,y-a,x,y+a,paint);paint.setStyle(Paint.Style.FILL);
                    continue;
                }
                int kind=i<5?(int)layout[147+i*2]:0,charges=i<5?(int)layout[148+i*2]:0;
                boolean spent=(kind&2)!=0 && charges<=0;
                if(icon) {
                    if(spent) {paint.setColor(Color.rgb(77,77,77));c.drawCircle(x,y,r*.86f,paint);}
                    glyph(c,i,x,y,r,held,spent);
                    if(spent){paint.setColor(0x59000000);c.drawCircle(x,y,r,paint);}
                }
                if((kind&1)!=0) {
                    float br=r*.30f,bx=x+r*.74f,by=y-r*.74f;
                    color(.11f,.094f,.078f,.95f);c.drawCircle(bx,by,br,paint);
                    paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(Math.max(1,br*.14f));color(.63f,.50f,.27f,1);
                    c.drawCircle(bx,by,br-Math.max(1,br*.08f),paint);paint.setStyle(Paint.Style.FILL);
                    caption(c,"Ⅱ",bx,by-br*1.15f*.62f,br*1.15f,1);
                }
                if((kind&2)!=0) hud.skillCharge(c,Math.max(charges,0),x+r*.66f,y+r*.42f,getWidth()/scale,getHeight()/scale);
                if(icon) continue;
                Bitmap label=hud.controlImage(i==0?"lbl_main.png":"lbl_f"+(i+(layout[132]!=0?4:0))+".png");
                float h=r*(i==0?.30f:.36f),w=label.getWidth()*h/label.getHeight();
                bitmap(c,label,x-w/2,y+r*.50f-h/2,w,h,1); continue;
            }
            boolean fixed=!navigation && (i==10 || i==11) && !cancelMap;
            if(fixed) {
                Bitmap image=i==11?mapArt:menuArt;sourceRect.set(held?36:0,0,held?72:36,36);
                float size=r*2*.62f;targetRect.set(x-size/2,y-size/2,x+size/2,y+size/2);
                paint.setAlpha(Math.round((held?1:.82f)*255));c.drawBitmap(image,sourceRect,targetRect,paint);paint.setAlpha(255);
            } else {
                String text=navigation && i<5?labels[i+2]:i==11?labels[cancelMap?8:7]:"";
                float size=(i==0?13:10)*k;caption(c,text,x,y-size*.6f,size,held?1:.84f);
            }
        }
        if(!navigation && layout[135]>0) {
            float x=layout[133],y=layout[134],r=layout[135];shell(c,x,y,r,layout[145]!=0);
            caption(c,layout[132]!=0?"Ⅱ":"Ⅰ",x,y-14*k*.8f,14*k,1);
        }
        hud.draw(c,getWidth()/scale,getHeight()/scale,layout,labels);
        if(layout[140]==0 && !navigation && layout[142]==0) {
            boolean moving=layout[144]!=0;float x=layout[48],y=layout[49],r=layout[50];
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(k);color(.66f,.53f,.32f,moving?.32f:.12f);
            c.drawCircle(x,y,r,paint);paint.setStyle(Paint.Style.FILL);
            bitmap(c,joyArt,layout[51]-23*k,layout[52]-23*k,46*k,46*k,moving?.8f:.18f);
            if(moving && (layout[51]!=x || layout[52]!=y)) {
                float angle=(float)Math.toDegrees(Math.atan2(layout[52]-y,layout[51]-x));
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2*k);color(.66f,.53f,.32f,.7f);
                targetRect.set(x-r-3*k,y-r-3*k,x+r+3*k,y+r+3*k);c.drawArc(targetRect,angle-30,60,false,paint);
                paint.setStyle(Paint.Style.FILL);
            }
        }
        c.restore();
    }
    private int freeSlot() {
        for (int slot = 0; slot < 16; slot++) {
            boolean used = false;
            for (int i = 0; i < slots.size(); i++) if (slots.valueAt(i) == slot) used = true;
            if (!used) return slot;
        }
        return -1;
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (!running) return false;
        int action = event.getActionMasked(), at = event.getActionIndex();
        if (debugTouchGate.consume(action == MotionEvent.ACTION_DOWN,
                action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL)) return true;
        if ((action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
                && fpsBox.contains(event.getX(at), event.getY(at))) {
            if (getContext() instanceof MainActivity) ((MainActivity)getContext()).showDebugPanel(true);
            return true;
        }
        if (action == MotionEvent.ACTION_CANCEL) { cancelTouches(); return true; }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int slot = freeSlot(); if (slot < 0) return true;
            slots.put(event.getPointerId(at), slot);
            float x = event.getX(at)/scale, y = event.getY(at)/scale;
            nativeTouch(0,slot,x,y);
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                int slot = slots.get(event.getPointerId(i), -1);
                if (slot >= 0) nativeTouch(1, slot, event.getX(i)/scale, event.getY(i)/scale);
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int slot = slots.get(event.getPointerId(at), -1);
            if (slot >= 0) {
                nativeTouch(2, slot, event.getX(at)/scale, event.getY(at)/scale);
                slots.delete(event.getPointerId(at));
            }
        }
        refreshState(); invalidate(); return true;
    }
}
