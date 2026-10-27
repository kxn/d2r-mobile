package com.d2rmobile.nativeport;

import java.io.File;
import java.io.IOException;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Map;
import java.util.Properties;
import java.util.stream.Stream;

/** Publication identity/type contracts only; no game or GPU execution claim. */
public final class VerifiedStartupProfileTest {
    private interface Checked { void run() throws Exception; }
    private static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    private static void rejects(Checked task) throws Exception {
        try { task.run(); } catch (IOException expected) { return; }
        throw new AssertionError("Invalid installed profile was accepted");
    }
    private static String sha(byte[] bytes) throws Exception {
        StringBuilder s = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes)) s.append(String.format("%02x", b));
        return s.toString();
    }
    private static final class Fixture implements AutoCloseable {
        final Path temp = Files.createTempDirectory("desktop-startup-contract-");
        final File runtime = temp.resolve("runtime").toFile();
        final File apk = temp.resolve("base.apk").toFile();
        final Properties values = new Properties();
        final String[] files = {"vulkan-driver/libvulkan_freedreno.so", "modules/rewrite-manifest.json",
                "shaders/.chain", "native-one-resources/native-layouts.bin", "launch-profile-receipt.json"};
        final String[] digests = {"driver_sha256", "modules_manifest_sha256", "shader_chain_sha256",
                "native_table_sha256", "receipt_sha256"};
        Fixture() throws Exception {
            runtime.mkdirs();
            byte[] apkBytes = "APK identity fixture, not a game".getBytes(StandardCharsets.UTF_8);
            Files.write(apk.toPath(), apkBytes);
            values.setProperty("apk_sha256", sha(apkBytes));
            for (int n = 0; n < files.length; n++) {
                Path p = runtime.toPath().resolve(files[n]); Files.createDirectories(p.getParent());
                byte[] bytes = ("Anchor fixture: " + files[n]).getBytes(StandardCharsets.UTF_8);
                Files.write(p, bytes); values.setProperty(digests[n], sha(bytes));
            }
            values.setProperty("schema", "1"); values.setProperty("name", "Verified Turnip");
            values.setProperty("device_fingerprint", "test/device/build");
            values.setProperty("vulkan_provider", "adreno"); values.setProperty("shared_guest_heap", "true");
            values.setProperty("native_table", files[3]);
            values.setProperty("native_pack_sha256", sha(new byte[]{1}));
            values.setProperty("native_producer_sha256", sha(new byte[]{2}));
            values.setProperty("pool_window_bytes", "1048576");
            values.setProperty("shared_lazy_windows", "true");
            values.setProperty("shared_image_raw_observe", "false");
            values.setProperty("driver_span_reference", "true");
            values.setProperty("pixel_local", "true");
            values.setProperty("pixel_local_shared_ds", "false");
        }
        Path profile() { return runtime.toPath().resolve("launch-profile.properties"); }
        void useV2() {
            values.setProperty("schema", "2");
            values.setProperty("inline_translation", "true");
            values.setProperty("translation_cpu_mask", "128");
            values.setProperty("stream_submits", "false");
            values.setProperty("turnip_autotune", "profiled");
        }
        void useV3() {
            useV2(); values.setProperty("schema", "3");
            values.setProperty("turnip_swapchain_ubwc", "true");
        }
        void useV4() {
            useV3(); values.setProperty("schema", "4");
            values.setProperty("guest_wait_budget_us", "30");
        }
        void useV5() {
            useV4(); values.setProperty("schema", "5");
            values.setProperty("record_state_cache", "true");
        }
        void store() throws Exception {
            try (Writer out = Files.newBufferedWriter(profile(), StandardCharsets.UTF_8)) { values.store(out, null); }
        }
        VerifiedStartupProfile load() throws Exception {
            return VerifiedStartupProfile.load(runtime, apk, "test/device/build");
        }
        @Override public void close() throws IOException {
            try (Stream<Path> paths = Files.walk(temp)) {
                Path[] entries = paths.sorted(Comparator.reverseOrder()).toArray(Path[]::new);
                for (Path p : entries) Files.delete(p);
            }
        }
    }
    public static void main(String[] args) throws Exception {
        check(VerifiedStartupProfile.ordinaryLauncher("android.intent.action.MAIN", true, false));
        check(!VerifiedStartupProfile.ordinaryLauncher("android.intent.action.MAIN", true, true));
        check(!VerifiedStartupProfile.ordinaryLauncher("android.intent.action.MAIN", false, false));
        check(!VerifiedStartupProfile.ordinaryLauncher(null, true, false));
        try (Fixture f = new Fixture()) {
            check(f.load() == null); // No profile preserves the original launch defaults.
            f.store(); Map<String,Object> extras = f.load().extras;
            check(Boolean.TRUE.equals(extras.get("shared_guest_heap")));
            check(extras.get("pool_window_bytes") instanceof Long);
            check(Long.valueOf(1048576).equals(extras.get("pool_window_bytes")));
            check("adreno".equals(extras.get("vulkan_provider")));
            check(Boolean.TRUE.equals(extras.get("async_submit")));
            check(Boolean.FALSE.equals(extras.get("run_vulkan_tests")));
            for (String dynamic : Arrays.asList("target_fps", "scale_3d", "game_res", "thermal_cap", "thermal_pause"))
                check(!extras.containsKey(dynamic));
            try { extras.put("scale_3d", 1f); throw new AssertionError("Mutable profile"); }
            catch (UnsupportedOperationException expected) { }
            f.values.setProperty("pool_window_bytes", Long.toString(1L << 40)); f.store();
            check(Long.valueOf(1L << 40).equals(f.load().extras.get("pool_window_bytes")));
        }
        try (Fixture f = new Fixture()) {
            f.useV2(); f.store(); Map<String,Object> extras = f.load().extras;
            check(Boolean.TRUE.equals(extras.get("inline_translation")));
            check(Integer.valueOf(128).equals(extras.get("translation_cpu_mask")));
            check(Boolean.TRUE.equals(extras.get("stream_submit_reference")));
            check("profiled".equals(extras.get("turnip_autotune")));
            for (String dynamic : Arrays.asList("target_fps", "scale_3d", "game_res", "thermal_cap"))
                check(!extras.containsKey(dynamic));
            for (String mode : Arrays.asList("default", "bandwidth", "prefer_sysmem")) {
                f.values.setProperty("turnip_autotune", mode); f.store();
                check(mode.equals(f.load().extras.get("turnip_autotune")));
            }
        }
        for (String[] entry : new String[][]{{"translation_cpu_mask","-1"},
                {"translation_cpu_mask","2147483648"}, {"translation_cpu_mask","+128"},
                {"turnip_autotune","invalid"}, {"inline_translation","yes"}, {"stream_submits","0"}}) {
            try (Fixture f = new Fixture()) {
                f.useV2(); f.values.setProperty(entry[0], entry[1]); f.store(); rejects(f::load);
            }
        }
        try (Fixture f = new Fixture()) {
            f.useV2(); f.values.setProperty("schema", "3");
            f.store(); rejects(f::load); // V3 cannot silently omit the policy.
            for (String value : Arrays.asList("true", "false")) {
                f.values.setProperty("turnip_swapchain_ubwc", value); f.store();
                check(Boolean.valueOf(value).equals(f.load().extras.get("turnip_swapchain_ubwc")));
            }
            f.values.setProperty("turnip_swapchain_ubwc", "yes"); f.store(); rejects(f::load);
            f.values.setProperty("schema", "2"); f.values.setProperty("turnip_swapchain_ubwc", "true");
            f.store(); rejects(f::load); // Older schemas still reject extra keys.
        }
        for (String value : Arrays.asList("0", "30", "100")) {
            try (Fixture f = new Fixture()) {
                f.useV4(); f.values.setProperty("guest_wait_budget_us", value); f.store();
                Map<String,Object> extras = f.load().extras;
                check(extras.get("guest_wait_budget_us") instanceof Integer);
                check(Integer.valueOf(value).equals(extras.get("guest_wait_budget_us")));
                check(Boolean.TRUE.equals(extras.get("turnip_swapchain_ubwc")));
                check(Boolean.TRUE.equals(extras.get("inline_translation")));
                check(Integer.valueOf(128).equals(extras.get("translation_cpu_mask")));
                check(Boolean.TRUE.equals(extras.get("stream_submit_reference")));
                check("profiled".equals(extras.get("turnip_autotune")));
            }
        }
        for (String value : Arrays.asList("-1", "101", "text", "+30", "030", "00", "-0",
                " 30", "30 ", "30.0", "0x1e", "2147483648", "999999999999999999999999999999")) {
            try (Fixture f = new Fixture()) {
                f.useV4(); f.values.setProperty("guest_wait_budget_us", value); f.store(); rejects(f::load);
            }
        }
        for (String key : Arrays.asList("guest_wait_budget_us", "turnip_swapchain_ubwc", "inline_translation",
                "translation_cpu_mask", "stream_submits", "turnip_autotune", "receipt_sha256")) {
            try (Fixture f = new Fixture()) { f.useV4(); f.values.remove(key); f.store(); rejects(f::load); }
        }
        for (String schema : Arrays.asList("1", "2", "3")) {
            try (Fixture f = new Fixture()) {
                if (schema.equals("2")) f.useV2();
                if (schema.equals("3")) f.useV3();
                f.store(); check(!f.load().extras.containsKey("guest_wait_budget_us"));
                f.values.setProperty("guest_wait_budget_us", "0"); f.store(); rejects(f::load);
            }
        }
        try (Fixture f = new Fixture()) {
            f.useV4(); f.values.setProperty("scale_3d", "1.0"); f.store(); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.useV4(); f.store();
            Files.write(f.profile(), "\nguest_wait_budget_us=30\n".getBytes(StandardCharsets.UTF_8),
                    java.nio.file.StandardOpenOption.APPEND);
            rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.useV4(); f.store();
            Files.write(f.apk.toPath(), new byte[]{9}); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.useV4(); f.store();
            Files.write(f.runtime.toPath().resolve("launch-profile-receipt.json"), new byte[]{9}); rejects(f::load);
        }
        for (String value : Arrays.asList("true", "false")) {
            try (Fixture f = new Fixture()) {
                f.useV5(); f.values.setProperty("record_state_cache", value); f.store();
                Map<String,Object> extras = f.load().extras;
                check(extras.get("record_state_cache") instanceof Boolean);
                check(Boolean.valueOf(value).equals(extras.get("record_state_cache")));
                check(Integer.valueOf(30).equals(extras.get("guest_wait_budget_us")));
                check(Boolean.TRUE.equals(extras.get("turnip_swapchain_ubwc")));
                check(Boolean.TRUE.equals(extras.get("inline_translation")));
                check(Integer.valueOf(128).equals(extras.get("translation_cpu_mask")));
                check(Boolean.TRUE.equals(extras.get("stream_submit_reference")));
                check("profiled".equals(extras.get("turnip_autotune")));
                for (String dynamic : Arrays.asList("target_fps", "scale_3d", "game_res", "thermal_cap"))
                    check(!extras.containsKey(dynamic));
            }
        }
        for (String value : Arrays.asList("", "1", "0", "yes", "True", "FALSE", " true", "false ", "null")) {
            try (Fixture f = new Fixture()) {
                f.useV5(); f.values.setProperty("record_state_cache", value); f.store(); rejects(f::load);
            }
        }
        try (Fixture f = new Fixture()) {
            f.useV5();
            for (String key : f.values.stringPropertyNames().toArray(new String[0])) {
                String original = (String)f.values.remove(key); f.store(); rejects(f::load);
                f.values.setProperty(key, original);
            }
        }
        for (String schema : Arrays.asList("1", "2", "3", "4")) {
            try (Fixture f = new Fixture()) {
                if (schema.equals("2")) f.useV2();
                if (schema.equals("3")) f.useV3();
                if (schema.equals("4")) f.useV4();
                f.store(); check(!f.load().extras.containsKey("record_state_cache"));
                f.values.setProperty("record_state_cache", "false"); f.store(); rejects(f::load);
            }
        }
        try (Fixture f = new Fixture()) {
            f.useV5(); f.store();
            Files.write(f.profile(), "\nrecord_state_cache=true\n".getBytes(StandardCharsets.UTF_8),
                    java.nio.file.StandardOpenOption.APPEND);
            rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.useV5(); f.values.setProperty("unexpected_policy", "true"); f.store(); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.useV5(); f.values.setProperty("guest_wait_budget_us", "101"); f.store(); rejects(f::load);
        }
        for (String changed : Arrays.asList("apk", "fingerprint", "driver", "modules", "shader", "table", "receipt")) {
            try (Fixture f = new Fixture()) {
                f.useV5(); f.store();
                if (changed.equals("fingerprint")) {
                    rejects(() -> VerifiedStartupProfile.load(f.runtime, f.apk, "different/build"));
                } else {
                    int anchor = Arrays.asList("driver", "modules", "shader", "table", "receipt").indexOf(changed);
                    Path file = anchor < 0 ? f.apk.toPath() : f.runtime.toPath().resolve(f.files[anchor]);
                    Files.write(file, new byte[]{9}); rejects(f::load);
                }
            }
        }
        for (String key : Arrays.asList("inline_translation", "translation_cpu_mask", "stream_submits", "turnip_autotune")) {
            try (Fixture f = new Fixture()) { f.useV2(); f.values.remove(key); f.store(); rejects(f::load); }
        }
        try (Fixture f = new Fixture()) {
            f.store(); rejects(() -> VerifiedStartupProfile.load(f.runtime, f.apk, "different/build"));
            Files.write(f.apk.toPath(), new byte[]{9}); rejects(f::load);
        }
        for (int n = 0; n < 5; n++) {
            try (Fixture f = new Fixture()) {
                f.store(); Files.write(f.runtime.toPath().resolve(f.files[n]), new byte[]{9}); rejects(f::load);
            }
        }
        String[][] invalid = {{"schema","2"}, {"vulkan_provider","panvk"}, {"shared_guest_heap","false"},
                {"pool_window_bytes","0"}, {"pool_window_bytes","4097"}, {"pool_window_bytes","-4096"},
                {"pool_window_bytes","9223372036854775808"}, {"pixel_local","yes"},
                {"native_pack_sha256","abcd"}, {"native_producer_sha256","bad"},
                {"native_table","native-one-resources/../native-layouts.bin"},
                {"native_table","/outside.bin"}, {"native_table","native-one-resources/./native-layouts.bin"}};
        for (String[] row : invalid) {
            try (Fixture f = new Fixture()) { f.values.setProperty(row[0],row[1]); f.store(); rejects(f::load); }
        }
        try (Fixture f = new Fixture()) {
            f.values.remove("receipt_sha256"); f.store(); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.values.setProperty("scale_3d", "1.0"); f.store(); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.values.setProperty("pixel_local", "false"); f.values.setProperty("pixel_local_shared_ds", "true");
            f.store(); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.store(); Files.write(f.profile(), "\nschema=1\n".getBytes(StandardCharsets.UTF_8), java.nio.file.StandardOpenOption.APPEND);
            rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            f.store(); Files.write(f.profile(), new byte[16385]); rejects(f::load);
        }
        try (Fixture f = new Fixture()) {
            Path outside = f.temp.resolve("outside.bin"); Files.write(outside,new byte[]{1});
            Path link = f.runtime.toPath().resolve(f.files[3]); Files.delete(link); Files.createSymbolicLink(link,outside);
            f.store(); rejects(f::load);
        }
        System.out.println("PASS verified desktop profile schemas 1-5: identities, exact fields, immutable typed cold options, canonical 0..100 us budget, strict recording-cache boolean, bounded input and failures");
    }
}
