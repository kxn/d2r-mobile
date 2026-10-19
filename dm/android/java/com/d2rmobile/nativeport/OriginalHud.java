// 原 DmTouchOverlay 的 v4 底栏：直接读取原生成器的几何、字形与图片。
// The original DmTouchOverlay v4 bar, using the upstream generator's geometry,
// glyphs and images. Values are snapshots from the original host HUD reader.
package com.d2rmobile.nativeport;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class OriginalHud {
    private final Context context;
    private final JSONObject ui;
    private final HashMap<String, Bitmap> images = new HashMap<>();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF rect = new RectF();
    OriginalHud(Context context) {
        this.context = context;
        try (InputStream stream = context.getAssets().open("hud_v4_ui/ui.json")) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] block = new byte[4096];
            for (int n; (n = stream.read(block)) != -1;) bytes.write(block, 0, n);
            ui = new JSONObject(bytes.toString("UTF-8"));
        } catch (IOException | JSONException failure) {
            throw new IllegalStateException("Original HUD metadata unavailable", failure);
        }
    }
    private Bitmap image(String name) {
        Bitmap image = images.get(name);
        if (image != null) return image;
        try (InputStream stream = context.getAssets().open("hud_v4_ui/" + name)) {
            image = BitmapFactory.decodeStream(stream);
            if (image == null) throw new IOException("Invalid PNG: " + name);
            images.put(name, image);
            return image;
        } catch (IOException failure) {
            throw new IllegalStateException("Original HUD image unavailable: " + name, failure);
        }
    }
    // DmTouchOverlay uses these same generated label images and badge glyphs
    // for the skill controls as well as the bottom bar.
    Bitmap controlImage(String name) { return image(name); }
    void skillCharge(Canvas canvas, int charges, float x, float baseline,
                     float width, float height) {
        int saved = canvas.save();
        try {
            float u = height/(float)ui.getJSONArray("ref").getDouble(1);
            if (!(u > 0)) return;
            float center = (float)ui.getDouble("center_x");
            canvas.translate(width/2-center*u, 0);
            canvas.scale(u, u);
            text(canvas, Integer.toString(Math.max(0, charges)),
                 ui.getJSONObject("badge").getJSONObject("glyphs"),
                 (x-width/2)/u+center, baseline/u, true);
        } catch (JSONException failure) {
            throw new IllegalStateException("Invalid original skill badge geometry", failure);
        } finally { canvas.restoreToCount(saved); }
    }
    private void bitmap(Canvas canvas, Bitmap image, float x, float y, float w, float h, float alpha) {
        rect.set(x, y, x+w, y+h);
        paint.setAlpha(Math.round(alpha*255));
        canvas.drawBitmap(image, null, rect, paint);
        paint.setAlpha(255);
    }
    private void text(Canvas canvas, String text, JSONObject glyphs, float x, float baseline,
                      boolean right) throws JSONException {
        float advance = 0;
        for (int i = 0; i < text.length(); ++i) {
            JSONObject glyph = glyphs.optJSONObject(text.substring(i, i+1));
            if (glyph != null) advance += glyph.getDouble("adv");
        }
        float pen = x - (right ? advance : advance/2);
        for (int i = 0; i < text.length(); ++i) {
            JSONObject glyph = glyphs.optJSONObject(text.substring(i, i+1));
            if (glyph == null) continue;
            if (!glyph.isNull("file")) {
                Bitmap image = image(glyph.getString("file"));
                bitmap(canvas, image, pen+(float)glyph.getDouble("dx"),
                       baseline+(float)glyph.getDouble("dy"), image.getWidth(), image.getHeight(), 1);
            }
            pen += glyph.getDouble("adv");
        }
    }
    private static int color(JSONArray rgb) throws JSONException {
        return Color.rgb(rgb.getInt(0), rgb.getInt(1), rgb.getInt(2));
    }
    private void digit(Canvas canvas, int value, float x, float y, float alpha) {
        Bitmap digit = image("diff_" + value + ".png");
        bitmap(canvas, digit, x-digit.getWidth()/2f, y-digit.getHeight()/2f,
               digit.getWidth(), digit.getHeight(), alpha);
    }
    void draw(Canvas canvas, float width, float height, float[] state, String[] labels) {
        if (state[136] == 0) return;
        int saved = canvas.save();
        try {
            float u = height/(float)ui.getJSONArray("ref").getDouble(1);
            canvas.translate(width/2-(float)ui.getDouble("center_x")*u, 0);
            canvas.scale(u, u);
            JSONObject base = ui.getJSONObject("base");
            Bitmap full = image("base_full.png");
            bitmap(canvas, full, base.getInt("x"), base.getInt("y"), full.getWidth(), full.getHeight(), 1);
            JSONArray slots = ui.getJSONArray("slots"), inner = ui.getJSONArray("inner");
            JSONArray menu = slots.getJSONArray(5);
            boolean alert = state[138] > 0 || state[139] != 0, held = state[143] != 0;
            Bitmap cross = image(alert ? (held ? "lvl_1.png" : "lvl_0.png") :
                                        (held ? "lvl_d1.png" : "lvl_d0.png"));
            // Same unspent-point glow as DmTouchOverlay.drawV4; quest-only
            // alerts retain the original red frame without the gold shadow.
            if (state[138] > 0) {
                paint.setShadowLayer(10, 0, 0, Color.argb(242, 255, 209, 89));
                bitmap(canvas, cross, menu.getInt(0), menu.getInt(1),
                       menu.getInt(2)-menu.getInt(0), menu.getInt(3)-menu.getInt(1), 1);
                paint.clearShadowLayer();
            }
            bitmap(canvas, cross, menu.getInt(0), menu.getInt(1),
                   menu.getInt(2)-menu.getInt(0), menu.getInt(3)-menu.getInt(1), 1);
            JSONObject exp = ui.getJSONObject("exp");
            float x0 = exp.getInt("x0"), x1 = exp.getInt("x1"), y = exp.getInt("y"), h = exp.getInt("h");
            paint.setStyle(Paint.Style.FILL); paint.setColor(color(exp.getJSONArray("track")));
            canvas.drawRect(x0, y, x1, y+h, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1);
            paint.setColor(color(exp.getJSONArray("edge")));
            canvas.drawRect(x0+.5f, y+.5f, x1-.5f, y+h-.5f, paint);
            paint.setStyle(Paint.Style.FILL); paint.setColor(color(exp.getJSONArray("fill")));
            canvas.drawRect(x0+1, y+1, x0+1+(x1-x0-2)*state[116]/1000, y+h-1, paint);
            paint.setColor(color(exp.getJSONArray("track")));
            for (int t = 1, ticks = exp.getInt("ticks"); t < ticks; ++t) {
                float x = x0+Math.round((x1-x0)*t/ticks);
                canvas.drawRect(x, y+1, x+1, y+h-1, paint);
            }
            JSONObject vital = ui.getJSONObject("vital"), level = ui.getJSONObject("lv");
            text(canvas, String.format(Locale.ROOT, labels[0], (int)state[117], (int)state[118]),
                 vital.getJSONObject("glyphs"), vital.getInt("left_x"), vital.getInt("baseline"), false);
            text(canvas, String.format(Locale.ROOT, labels[1], (int)state[119], (int)state[120]),
                 vital.getJSONObject("glyphs"), vital.getInt("right_x"), vital.getInt("baseline"), false);
            text(canvas, String.format(Locale.ROOT, "Lv %d · %d%%", (int)state[115], (int)state[116]/10),
                 level.getJSONObject("glyphs"), (float)level.getDouble("x"), level.getInt("baseline"), false);
            JSONObject badge = ui.getJSONObject("badge");
            for (int i = 0; i < 4; ++i) if (state[121+i] > 0) {
                JSONArray slot = slots.getJSONArray(i+1);
                text(canvas, Integer.toString((int)state[121+i]), badge.getJSONObject("glyphs"),
                     slot.getInt(0)+inner.getInt(2)+badge.getInt("dx"),
                     slot.getInt(1)+inner.getInt(3)+badge.getInt("dy"), true);
            }
            JSONArray difficulty = slots.getJSONArray(0);
            float cx = (inner.getInt(0)+inner.getInt(2)+1)/2f;
            float cy = (inner.getInt(1)+inner.getInt(3)+1)/2f;
            int players = (int)state[125];
            if (players >= 1 && players <= 8)
                digit(canvas, players, difficulty.getInt(0)+cx, difficulty.getInt(1)+cy, 1);
            if (state[137] != 0) {
                JSONObject pick = ui.getJSONObject("pick");
                Bitmap tile = image("slot.png");
                for (int i = 0; i < 8; ++i) {
                    float px = pick.getInt("center_x")+(i-3.5f)*pick.getInt("pitch");
                    float py = (float)pick.getDouble("y"), alpha = i+1 == players ? 1 : .82f;
                    bitmap(canvas, tile, px-tile.getWidth()/2f, py-tile.getHeight()/2f,
                           tile.getWidth(), tile.getHeight(), alpha);
                    digit(canvas, i+1, px, py, alpha);
                }
            }
        } catch (JSONException failure) {
            throw new IllegalStateException("Invalid original HUD geometry", failure);
        } finally { canvas.restoreToCount(saved); }
    }
}
