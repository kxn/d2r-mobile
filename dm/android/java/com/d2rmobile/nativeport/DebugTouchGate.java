package com.d2rmobile.nativeport;

/** Preserve cancellation until a fresh gesture, including close during multitouch. */
final class DebugTouchGate {
    private boolean blocked, drain;
    void block(boolean value) { blocked = value; drain = true; }
    boolean consume(boolean down, boolean finished) {
        if (blocked) return true;
        if (down) drain = false;
        boolean consumed = drain;
        if (finished) drain = false;
        return consumed;
    }
}
