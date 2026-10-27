#!/system/bin/sh
# Diagnostic packaging only: reuse the existing NVN command-recording counts.
# cs_dump reports every 300 frames; its recording locks affect CPU work, so
# this APK must not be used as a performance baseline. No native/game changes.
export DM_NVN_CMDSTAT=1
exec "$@"
