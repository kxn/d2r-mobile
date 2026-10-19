#pragma once
// Android-only, synchronous pure host callback. No guest calls, ExitThread,
// nonlocal jump or C++ exception may cross the callback. Not signal-safe.
// Returns zero WITHOUT calling fn when no proven pthread stack is available.
// The callback may make nested host calls, which stay on the current stack.
int dm_thread_host_call(void (*fn)(void *),void *argument);
