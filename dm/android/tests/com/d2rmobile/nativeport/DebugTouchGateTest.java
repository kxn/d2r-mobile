package com.d2rmobile.nativeport;

public final class DebugTouchGateTest {
    private static void expect(boolean expected, boolean value) {
        if (expected != value) throw new AssertionError("gesture leaked/was lost");
    }
    public static void main(String[] args) {
        DebugTouchGate g = new DebugTouchGate();
        expect(false, g.consume(true, false)); // Original game gesture.
        g.block(true); // Opening on second finger cancels the existing native slots.
        expect(true, g.consume(false, false)); // MOVE/POINTER_UP while visible.
        expect(true, g.consume(false, true));
        g.block(false); // Hardware Back may close while other fingers remain down.
        expect(true, g.consume(false, false));
        expect(true, g.consume(false, true)); // No UP reaches a skill/menu.
        expect(false, g.consume(true, false)); // Next fresh DOWN is playable.
        expect(false, g.consume(false, true));
        g.block(true); g.block(false); // Open and close without a remaining sequence.
        expect(false, g.consume(true, false));
        expect(false, g.consume(false, true));
        g.block(true); expect(true, g.consume(true, false)); // New touches stay blocked.
        g.block(false); expect(true, g.consume(false, true)); // CANCEL drains the sequence.
        expect(false, g.consume(true, false));
        System.out.println("DebugTouchGate: multitouch/Back/CANCEL/fresh-DOWN passed");
    }
}
