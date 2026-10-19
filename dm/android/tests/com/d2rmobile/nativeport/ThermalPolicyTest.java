package com.d2rmobile.nativeport;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.PriorityQueue;

/** Deterministic UI queue tests; virtual time is not a performance measurement. */
public final class ThermalPolicyTest {
    private static final class Queue implements ThermalPolicy.Scheduler {
        private static final class Message {
            final long when, order;
            final Runnable task;
            Message(long when, long order, Runnable task) {
                this.when = when; this.order = order; this.task = task;
            }
        }
        long clock, order;
        final PriorityQueue<Message> messages = new PriorityQueue<>(
                Comparator.comparingLong((Message m) -> m.when).thenComparingLong(m -> m.order));
        @Override public long now() { return clock; }
        @Override public void postDelayed(Runnable task, long delay) {
            if (delay < 0) throw new AssertionError("negative delay");
            messages.add(new Message(clock + delay, order++, task));
        }
        @Override public void removeCallbacks(Runnable task) {
            messages.removeIf(m -> m.task == task);
        }
        void until(long end) {
            if (end < clock) throw new AssertionError("clock moved backwards");
            while (!messages.isEmpty() && messages.peek().when <= end) {
                Message m = messages.remove(); clock = m.when; m.task.run();
            }
            clock = end;
        }
    }
    private static final class Fixture implements ThermalPolicy.Actions {
        final Queue queue = new Queue();
        final ThermalPolicy policy = new ThermalPolicy(queue, this);
        final List<String> changes = new ArrayList<>();
        int limit, pauses;
        Fixture() { policy.setActive(true); }
        @Override public void limit(int fps) {
            limit = fps; changes.add("limit " + fps + " @" + queue.clock);
        }
        @Override public void pause() {
            pauses++; changes.add("pause @" + queue.clock);
        }
        void status(int status) { policy.status(status); }
        void at(long time, int status) { queue.until(time); status(status); }
    }
    private static void eq(Object expected, Object actual) {
        if (!expected.equals(actual)) throw new AssertionError("expected " + expected + ", got " + actual);
    }
    private static void severeDeadlineIsNotDebounced() {
        Fixture f = new Fixture(); f.status(3);
        for (int i = 1; i <= 44; i++) f.at(i * 1000L, 3);
        f.queue.until(44999); eq(0, f.pauses);
        f.queue.until(45000); eq(1, f.pauses);
        f.at(46000, 3); f.queue.until(100000); eq(1, f.pauses);
        eq("pause @45000", f.changes.get(1));
    }
    private static void queuedCriticalThenSevereCannotCancelPause() {
        Fixture f = new Fixture();
        // Both notifications predate the old implementation's zero-delay
        // pause. That pause used to be removed by the queued downgrade.
        f.queue.postDelayed(() -> f.status(4), 0);
        f.queue.postDelayed(() -> f.status(3), 0);
        f.queue.until(0); eq(1, f.pauses);
        f.status(4); f.status(5); f.status(6);
        f.queue.until(100000); eq(1, f.pauses);
        eq("pause @0", f.changes.get(1));
    }
    private static void criticalEscalationPausesImmediately() {
        Fixture f = new Fixture(); f.status(3); f.at(44000, 4);
        eq(1, f.pauses); eq("pause @44000", f.changes.get(1));
        f.at(44001, 3); f.queue.until(100000); eq(1, f.pauses);
    }
    private static void lowBandOscillationPreservesPartialDeadline() {
        Fixture f = new Fixture(); f.status(3); f.at(1000, 0);
        f.at(11000, 1); f.at(21000, 0);
        f.queue.until(30999); eq(20, f.limit);
        f.queue.until(31000); eq(30, f.limit);
        // Full release still needs sixty seconds of NONE, starting at21000.
        f.queue.until(80999); eq(30, f.limit);
        f.queue.until(81000); eq(0, f.limit); eq(0, f.pauses);
        eq("limit 30 @31000", f.changes.get(1));
    }
    private static void moderateBreaksPartialRecoveryButRetainsTwenty() {
        Fixture f = new Fixture(); f.status(3); f.at(1000, 0);
        f.at(30000, 2); f.at(40000, 1);
        f.queue.until(69999); eq(20, f.limit);
        f.queue.until(70000); eq(30, f.limit); eq(0, f.pauses);
    }
    private static void noneMustBeContinuousForFullRecovery() {
        Fixture f = new Fixture(); f.status(1); f.at(1000, 0);
        f.at(60000, 1); f.at(61000, 0);
        f.queue.until(120999); eq(30, f.limit);
        f.queue.until(121000); eq(0, f.limit);
    }
    private static void realDropBelowSevereEndsHotEpisode() {
        Fixture f = new Fixture(); f.status(3); f.at(44000, 2);
        f.queue.until(60000); eq(0, f.pauses); eq(20, f.limit);
        f.at(61000, 3); f.queue.until(105999); eq(0, f.pauses);
        f.queue.until(106000); eq(1, f.pauses);
    }
    private static void unknownDoesNotProveCooling() {
        Fixture f = new Fixture(); f.status(3); f.at(10000, -1);
        f.queue.until(45000); eq(1, f.pauses); eq(20, f.limit);
        Fixture g = new Fixture(); g.status(3); g.at(1000, 0);
        g.at(20000, -1); g.queue.until(90000);
        eq(20, g.limit); eq(0, g.pauses);
        g.at(100000, 0); g.queue.until(129999); eq(20, g.limit);
        g.queue.until(130000); eq(30, g.limit);
        g.queue.until(160000); eq(0, g.limit);
    }
    private static void lifecycleCancelsTimersAndRequiresFreshStatus() {
        Fixture f = new Fixture(); f.status(3); f.queue.until(40000);
        f.policy.setActive(false); f.queue.until(100000);
        eq(0, f.pauses); eq(20, f.limit); eq(0, f.queue.messages.size());
        f.policy.setActive(true); f.status(-1); f.queue.until(200000);
        eq(0, f.pauses); eq(20, f.limit);
        f.status(4); eq(1, f.pauses);
        f.policy.setActive(false); f.policy.setActive(true); f.status(4);
        eq(2, f.pauses); // A new foreground critical session pauses once again.
        Fixture g = new Fixture(); g.status(1); g.at(1000, 0);
        g.queue.until(60000); g.policy.setActive(false);
        g.queue.until(100000); eq(30, g.limit); eq(0, g.queue.messages.size());
    }
    private static void staleDispatchCannotCompleteNewIntervalEarly() {
        Fixture f = new Fixture(); f.status(3); f.at(1000, 0);
        Runnable stale = f.queue.messages.stream().filter(m -> m.when == 31000)
                .findFirst().get().task;
        f.at(30000, 2); f.at(30500, 0);
        f.queue.until(31000); stale.run(); eq(20, f.limit);
        f.queue.until(60499); eq(20, f.limit);
        f.queue.until(60500); eq(30, f.limit);
        eq(1L, f.changes.stream().filter(s -> s.startsWith("limit 30")).count());
    }
    private static void noNewDefaultLimitOrDuplicateActions() {
        Fixture f = new Fixture(); f.status(-1); eq(0, f.changes.size());
        f.status(0); f.status(0); f.queue.until(100000);
        eq(0, f.limit); eq(1, f.changes.size()); eq("limit 0 @0", f.changes.get(0));
        f.status(1); f.status(1); f.status(2); eq(30, f.limit); eq(2, f.changes.size());
        f.policy.setActive(false); f.status(4); eq(0, f.pauses);
    }
    private static void recreatedActivitySynchronizesExistingNativeLimit() {
        Fixture f = new Fixture();
        f.limit = 20; // Existing native host survived the previous Activity.
        f.status(-1); eq(20, f.limit); eq(0, f.changes.size());
        f.status(0); eq(0, f.limit); eq("limit 0 @0", f.changes.get(0));
        f.status(0); f.queue.until(60000); eq(1, f.changes.size());
    }
    public static void main(String[] args) {
        severeDeadlineIsNotDebounced();
        queuedCriticalThenSevereCannotCancelPause();
        criticalEscalationPausesImmediately();
        lowBandOscillationPreservesPartialDeadline();
        moderateBreaksPartialRecoveryButRetainsTwenty();
        noneMustBeContinuousForFullRecovery();
        realDropBelowSevereEndsHotEpisode();
        unknownDoesNotProveCooling();
        lifecycleCancelsTimersAndRequiresFreshStatus();
        staleDispatchCannotCompleteNewIntervalEarly();
        noNewDefaultLimitOrDuplicateActions();
        recreatedActivitySynchronizesExistingNativeLimit();
        System.out.println("ThermalPolicy: 12 queue/virtual-time regressions passed");
    }
}
