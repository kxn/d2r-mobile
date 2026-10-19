#!/system/bin/sh
# IE TLS 必须在动态链接器创建初始 TLS 布局时加载；随后 Java 取得同一个库。
# Load IE TLS while the linker builds the initial TLS layout; Java later obtains the same library.
DM_ANDROID_LIB="$(dirname "$0")"
export LD_PRELOAD="$DM_ANDROID_LIB/libdmhost_android.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$@"
