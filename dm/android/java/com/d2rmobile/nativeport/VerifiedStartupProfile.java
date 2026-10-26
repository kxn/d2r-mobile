package com.d2rmobile.nativeport;

import java.io.File;
import java.io.FileInputStream;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Properties;
import java.util.Set;

/** Immutable cold-start selection; no Android dependency or render-loop work. */
final class VerifiedStartupProfile {
    private static final String[] KEYS = {
        "schema", "name", "apk_sha256", "device_fingerprint", "driver_sha256",
        "modules_manifest_sha256", "shader_chain_sha256", "native_table",
        "native_table_sha256", "native_pack_sha256", "native_producer_sha256",
        "receipt_sha256", "vulkan_provider", "shared_guest_heap", "pool_window_bytes",
        "shared_lazy_windows", "shared_image_raw_observe", "driver_span_reference",
        "pixel_local", "pixel_local_shared_ds"
    };
    private static final Set<String> REQUIRED = new HashSet<>(Arrays.asList(KEYS));
    private static final Set<String> REQUIRED_V2 = new HashSet<>(REQUIRED);
    static {
        REQUIRED_V2.addAll(Arrays.asList("inline_translation", "translation_cpu_mask",
                "stream_submits", "turnip_autotune"));
    }
    final String name;
    final Map<String, Object> extras;

    private VerifiedStartupProfile(String name, Map<String, Object> extras) {
        this.name = name;
        this.extras = Collections.unmodifiableMap(extras);
    }

    static boolean ordinaryLauncher(String action, boolean launcherCategory, boolean hasExtras) {
        return "android.intent.action.MAIN".equals(action) && launcherCategory && !hasExtras;
    }

    static VerifiedStartupProfile load(File runtime, File apk, String fingerprint) throws IOException {
        File root = runtime.getCanonicalFile();
        File candidate = new File(root, "launch-profile.properties");
        if (!candidate.exists()) return null;
        File profile = privateFile(root, "launch-profile.properties");
        if (profile.length() <= 0 || profile.length() > 16384) throw invalid("profile size");
        Properties values = new Properties() {
            @Override public synchronized Object put(Object key, Object value) {
                if (containsKey(key)) throw new IllegalArgumentException("duplicate startup profile key");
                return super.put(key, value);
            }
        };
        byte[] encoded = new byte[16385];
        int size = 0;
        try (FileInputStream input = new FileInputStream(profile)) {
            while (size < encoded.length) {
                int n = input.read(encoded, size, encoded.length - size);
                if (n == -1) break;
                size += n;
            }
        }
        if (size == 0 || size == encoded.length) throw invalid("profile size");
        try (InputStreamReader reader = new InputStreamReader(
                new ByteArrayInputStream(encoded, 0, size), StandardCharsets.UTF_8)) {
            values.load(reader);
        } catch (IllegalArgumentException error) {
            throw invalid("profile properties", error);
        }
        String schema = values.getProperty("schema");
        boolean version2 = "2".equals(schema);
        if (!version2 && !"1".equals(schema)) throw invalid("profile schema");
        if (!values.stringPropertyNames().equals(version2 ? REQUIRED_V2 : REQUIRED))
            throw invalid("profile fields");
        String name = values.getProperty("name");
        if (name.isEmpty() || name.length() > 64 || name.indexOf('\n') >= 0 || name.indexOf('\r') >= 0)
            throw invalid("profile name");
        if (!fingerprint.equals(values.getProperty("device_fingerprint"))) throw invalid("device build");
        if (!"adreno".equals(values.getProperty("vulkan_provider")) || !flag(values, "shared_guest_heap"))
            throw invalid("shared/native provider");
        long window;
        try { window = Long.parseLong(values.getProperty("pool_window_bytes")); }
        catch (NumberFormatException error) { throw invalid("pool window", error); }
        if (window < 4096 || (window & (window - 1)) != 0) throw invalid("finite pool window");
        boolean pixelLocal = flag(values, "pixel_local");
        boolean sharedDepth = flag(values, "pixel_local_shared_ds");
        if (sharedDepth && !pixelLocal) throw invalid("shared depth without pixel-local");
        String relativeTable = values.getProperty("native_table");
        if (!relativeTable.startsWith("native-one-resources/")) throw invalid("native table location");
        File table = privateFile(root, relativeTable);
        // The publisher verifies the full inventories. These anchors bind its
        // installed tuple; existing native code still authenticates payloads.
        byte[] scratch = new byte[65536];
        verify(apk, values, "apk_sha256", scratch);
        verify(privateFile(root, "vulkan-driver/libvulkan_freedreno.so"), values, "driver_sha256", scratch);
        verify(privateFile(root, "modules/rewrite-manifest.json"), values, "modules_manifest_sha256", scratch);
        verify(privateFile(root, "shaders/.chain"), values, "shader_chain_sha256", scratch);
        verify(table, values, "native_table_sha256", scratch);
        verify(privateFile(root, "launch-profile-receipt.json"), values, "receipt_sha256", scratch);
        String pack = digestValue(values, "native_pack_sha256");
        String producer = digestValue(values, "native_producer_sha256");
        Map<String, Object> extras = new LinkedHashMap<>();
        extras.put("run_game", true);
        extras.put("run_vulkan_tests", false);
        extras.put("validate", false);
        extras.put("gpu_profile", false);
        extras.put("async_submit", true);
        extras.put("external_profile", true);
        extras.put("vulkan_provider", "adreno");
        extras.put("shared_guest_heap", true);
        extras.put("native_resource_table", table.getPath());
        extras.put("native_resource_pack_sha256", pack);
        extras.put("native_resource_producer_sha256", producer);
        extras.put("pool_window_bytes", window);
        extras.put("shared_lazy_windows", flag(values, "shared_lazy_windows"));
        extras.put("shared_image_raw_observe", flag(values, "shared_image_raw_observe"));
        extras.put("driver_span_reference", flag(values, "driver_span_reference"));
        extras.put("pixel_local", pixelLocal);
        extras.put("pixel_local_shared_ds", sharedDepth);
        if (version2) {
            extras.put("inline_translation", flag(values, "inline_translation"));
            String mask = values.getProperty("translation_cpu_mask");
            if (!mask.matches("0|[1-9][0-9]*")) throw invalid("translation CPU mask");
            try { extras.put("translation_cpu_mask", Integer.parseInt(mask)); }
            catch (NumberFormatException error) { throw invalid("translation CPU mask", error); }
            extras.put("stream_submit_reference", !flag(values, "stream_submits"));
            String autotune = values.getProperty("turnip_autotune");
            if (!Arrays.asList("default", "bandwidth", "profiled", "prefer_sysmem").contains(autotune))
                throw invalid("Turnip autotune");
            extras.put("turnip_autotune", autotune);
        }
        return new VerifiedStartupProfile(name, extras);
    }

    private static File privateFile(File root, String relative) throws IOException {
        if (relative.isEmpty() || relative.startsWith("/") || relative.indexOf('\\') >= 0)
            throw invalid("private path");
        for (String part : relative.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..")) throw invalid("private path component");
        File file = new File(root, relative).getCanonicalFile();
        if (!file.getPath().startsWith(root.getPath() + File.separator) || !file.isFile())
            throw invalid("missing or escaped private file");
        return file;
    }

    private static boolean flag(Properties values, String key) throws IOException {
        String text = values.getProperty(key);
        if (!"true".equals(text) && !"false".equals(text)) throw invalid("boolean " + key);
        return "true".equals(text);
    }

    private static String digestValue(Properties values, String key) throws IOException {
        String text = values.getProperty(key);
        if (text == null || !text.matches("[0-9a-f]{64}")) throw invalid("SHA256 " + key);
        return text;
    }

    private static void verify(File file, Properties values, String key, byte[] scratch) throws IOException {
        String expected = digestValue(values, key);
        MessageDigest digest;
        try { digest = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException error) { throw invalid("SHA256 unavailable", error); }
        try (FileInputStream input = new FileInputStream(file)) {
            int n;
            while ((n = input.read(scratch)) != -1) digest.update(scratch, 0, n);
        }
        StringBuilder actual = new StringBuilder(64);
        for (byte b : digest.digest()) actual.append(Character.forDigit((b >>> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
        if (!expected.contentEquals(actual)) throw invalid("mismatched " + key);
    }

    private static IOException invalid(String what) { return new IOException("Installed startup profile: " + what); }
    private static IOException invalid(String what, Exception cause) { return new IOException("Installed startup profile: " + what, cause); }
}
