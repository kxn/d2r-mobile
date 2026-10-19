#!/usr/bin/env bash
# Source this file from Bash. Tools and private game data stay outside source.
D2R_REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
export D2R_REPO_ROOT
export DM_ROOT="${DM_ROOT:-$D2R_REPO_ROOT/work}"
export DOTNET_ROOT="$D2R_REPO_ROOT/.local/dotnet"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_NOLOGO=1
export ANDROID_HOME="$D2R_REPO_ROOT/.local/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export PATH="$D2R_REPO_ROOT/.local/venv/bin:$DOTNET_ROOT:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$D2R_REPO_ROOT/.local/src/hactool:$PATH"
# Default to CPU Vulkan for tooling checks; never select a compute GPU silently.
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
