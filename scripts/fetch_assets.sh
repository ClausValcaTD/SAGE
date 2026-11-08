#!/usr/bin/env bash
# Fetch the reference material used by SAGE.
#
#   references/model2.cpp, references/model2.h  MAME Model 2 driver (ROM_START)
#   references/supermodel/                      Model 3 emulator (architecture ref)
#
# The game ROMs (roms/doa.zip) are NOT downloaded here. Dead or Alive is
# copyrighted material; users must supply their own legally obtained ROM set.
# Place it at roms/doa.zip before running the emulator.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
refs="$root/references"
roms="$root/roms"
mkdir -p "$refs" "$roms"

mame_base="https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sega"

echo "==> MAME Model 2 driver sources"
curl -fL --retry 3 -o "$refs/model2.cpp" "$mame_base/model2.cpp"
curl -fL --retry 3 -o "$refs/model2.h" "$mame_base/model2.h"

echo "==> Supermodel (shallow clone)"
if [ ! -d "$refs/supermodel/.git" ]; then
    git clone --depth 1 https://github.com/trzy/Supermodel.git "$refs/supermodel"
else
    echo "    already present, skipping"
fi

if [ ! -f "$roms/doa.zip" ]; then
    echo "==> Dead or Alive ROM set not found at roms/doa.zip"
    echo "    SAGE does not distribute game ROMs. Supply your own legally"
    echo "    obtained 'doa' set (MAME-compatible) at roms/doa.zip."
fi

echo "==> Done"
ls -la "$roms" 2>/dev/null || true
