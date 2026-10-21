package com.d2rmobile.nativeport;

/** Public thermal-status policy; all calls and callbacks belong to the UI thread. */
final class ThermalPolicy {
    // PowerManager constants, kept here so deadline tests need no Android runtime.
    static final int NONE = 0, LIGHT = 1, SEVERE = 3, CRITICAL = 4, SHUTDOWN = 6;
    static final long SEVERE_MS = 45000, PARTLY_RECOVERED_MS = 30000, RECOVERED_MS = 60000;
    interface Scheduler {
        long now();
        void postDelayed(Runnable task, long delay);
        void removeCallbacks(Runnable task);
    }
    interface Actions {
        void limit(int fps);
        void pause();
    }
    private final Scheduler scheduler;
    private final Actions actions;
    private boolean active, known, limited, severeLimited, pauseIssued;
    private boolean limitEnabled = true, pauseEnabled = true;
    // The native host can outlive an Activity. Apply the first known limit,
    // including zero, instead of assuming its existing atomic is already zero.
    private int status = -1, limit = -1;
    private long severeSince = -1, partlyRecoveredSince = -1, recoveredSince = -1;
    private final Runnable severePause = this::severeDeadline;
    private final Runnable partlyRecovered = this::partlyRecoveredDeadline;
    private final Runnable recovered = this::recoveredDeadline;

    ThermalPolicy(Scheduler scheduler, Actions actions) {
        this.scheduler = scheduler;
        this.actions = actions;
    }
    boolean limitEnabled() { return limitEnabled; }
    boolean pauseEnabled() { return pauseEnabled; }
    void setLimitEnabled(boolean enabled) {
        if (limitEnabled == enabled) return;
        limitEnabled = enabled;
        // The switch also releases Android's adaptive-scale thermal constraint
        // through the existing Actions.limit/JNI path, without altering game FPS.
        limited = severeLimited = false;
        cancelRecovery();
        applyLimit(0);
        if (known) status(status);
    }
    void setPauseEnabled(boolean enabled) {
        if (pauseEnabled == enabled) return;
        pauseEnabled = enabled;
        severeSince = -1;
        scheduler.removeCallbacks(severePause);
        pauseIssued = false;
        // A new enable starts a fresh severe interval; current CRITICAL is immediate.
        if (enabled && known) status(status);
    }
    void setActive(boolean value) {
        if (active == value) return;
        active = value;
        cancelDeadlines();
        // A foreground session needs a fresh public status before starting a
        // recovery clock or pausing on an old reading. Keep its reduced limit.
        known = false;
        status = -1;
        pauseIssued = false;
    }
    void status(int next) {
        if (next < NONE || next > SHUTDOWN) {
            known = false;
            // Missing status proves no cooling. Retain an already armed hot
            // episode and its limit, but require fresh evidence for recovery.
            cancelRecovery();
            return;
        }
        known = true;
        status = next;
        if (next >= LIGHT) {
            limited = true;
            if (next >= SEVERE) severeLimited = true;
            applyLimit(severeLimited ? 20 : 30);
        } else if (!limited) applyLimit(0);
        if (!active || pauseIssued) return;
        if (pauseEnabled && next >= CRITICAL) {
            // Run on this UI callback, not as a zero-delay message that a
            // previously queued downgrade notification could cancel.
            pauseOnce();
            return;
        }
        if (pauseEnabled && next >= SEVERE) {
            if (severeSince < 0) {
                severeSince = scheduler.now();
                scheduler.postDelayed(severePause, SEVERE_MS);
            }
        } else {
            severeSince = -1;
            scheduler.removeCallbacks(severePause);
        }
        if (severeLimited && next <= LIGHT) {
            if (partlyRecoveredSince < 0) {
                partlyRecoveredSince = scheduler.now();
                scheduler.postDelayed(partlyRecovered, PARTLY_RECOVERED_MS);
            }
        } else {
            partlyRecoveredSince = -1;
            scheduler.removeCallbacks(partlyRecovered);
        }
        if (limited && next == NONE) {
            if (recoveredSince < 0) {
                recoveredSince = scheduler.now();
                scheduler.postDelayed(recovered, RECOVERED_MS);
            }
        } else {
            recoveredSince = -1;
            scheduler.removeCallbacks(recovered);
        }
    }
    private void applyLimit(int fps) {
        if (!limitEnabled) fps = 0;
        if (limit == fps) return;
        limit = fps;
        actions.limit(fps);
    }
    private boolean due(long since, long interval, Runnable task) {
        long remaining = interval - (scheduler.now() - since);
        if (remaining <= 0) return true;
        // A removed message can already be dispatching. Never let that stale
        // callback complete a newly started interval early or add duplicates.
        scheduler.removeCallbacks(task);
        scheduler.postDelayed(task, remaining);
        return false;
    }
    private void severeDeadline() {
        if (pauseEnabled && active && !pauseIssued && severeSince >= 0 && (!known || status >= SEVERE)
                && due(severeSince, SEVERE_MS, severePause)) pauseOnce();
    }
    private void partlyRecoveredDeadline() {
        if (active && !pauseIssued && known && status <= LIGHT && severeLimited
                && partlyRecoveredSince >= 0
                && due(partlyRecoveredSince, PARTLY_RECOVERED_MS, partlyRecovered)) {
            partlyRecoveredSince = -1;
            severeLimited = false;
            applyLimit(30);
        }
    }
    private void recoveredDeadline() {
        if (active && !pauseIssued && known && status == NONE && limited
                && recoveredSince >= 0 && due(recoveredSince, RECOVERED_MS, recovered)) {
            limited = severeLimited = false;
            cancelRecovery();
            applyLimit(0);
        }
    }
    private void pauseOnce() {
        if (!pauseEnabled || !active || pauseIssued) return;
        pauseIssued = true;
        cancelDeadlines();
        actions.pause();
    }
    private void cancelRecovery() {
        partlyRecoveredSince = recoveredSince = -1;
        scheduler.removeCallbacks(partlyRecovered);
        scheduler.removeCallbacks(recovered);
    }
    private void cancelDeadlines() {
        severeSince = -1;
        scheduler.removeCallbacks(severePause);
        cancelRecovery();
    }
}
