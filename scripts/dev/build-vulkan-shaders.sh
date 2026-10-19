#!/usr/bin/env bash
# 用原资源工具提取着色器包，用原 dmsh_xlate 翻译并校验 Vulkan 缓存。
# Extract shader packs with the original asset tool, then translate and validate with the original dmsh_xlate.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$D2R_REPO_ROOT"
# Keep the validated Vulkan optimizations in ordinary builds. Explicit values
# still select reference variants; the original Metal build defaults are intact.
export XLATE_DEBUG="${XLATE_DEBUG:-0}"
export DMSH_VK_STORAGE_BIAS="${DMSH_VK_STORAGE_BIAS:-1}"
export DMSH_VK_UNIFORM_SPECIALIZATION="${DMSH_VK_UNIFORM_SPECIALIZATION:-1}"
if [[ -z ${XLATE:-} ]]; then
  # Program.cs's embedded fingerprint does not cover its shader-library
  # dependency. Build the dependency too, so a stale AstOptimizer DLL cannot
  # silently pair with the current source fingerprint.
  dotnet build dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj \
    --artifacts-path "$D2R_REPO_ROOT/.local/build/dotnet" \
    -c Release --nologo
  export XLATE="$D2R_REPO_ROOT/.local/build/dotnet/bin/dmsh_xlate/release/dmsh_xlate"
fi
if [[ ! -x "$XLATE" ]]; then
  echo 'Build dm/gfx/tools/dmsh_xlate with dotnet, then set XLATE to its executable.' >&2
  exit 1
fi
export DMX_ROOT="$DM_ROOT/data/switch/3.3.94012/Update/RomFS/data"
packs="$DM_ROOT/build/cache/dm/dmsh"
mkdir -p "$packs"
python - "$packs" <<'PY'
from pathlib import Path
import subprocess,sys
root=Path(sys.argv[1])
rows=subprocess.check_output(['python','dm/tools/dmx.py','list','data:data/hd/shaders/*.bin'],text=True).splitlines()
if len(rows)!=162: raise SystemExit(f'Expected 162 original shader packs, found {len(rows)}')
for row in rows:
    path=row.split('\t')[0]
    output=root/Path(path).name
    if not output.is_file():
        subprocess.run(['python','dm/tools/dmx.py','get',path,str(output)],check=True,stdout=subprocess.DEVNULL)
PY
DMSH_TARGET=vulkan J="${J:-4}" python dm/gfx/tools/dmsh_build.py "$packs" "$DM_ROOT/build/cache/vulkan_shader_cache" "$@"
