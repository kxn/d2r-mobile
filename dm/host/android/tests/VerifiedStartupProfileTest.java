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
        System.out.println("PASS verified desktop profile: identities, explicit-launch isolation, immutable typed cold options, user settings, bounded input and failures");
    }
}
