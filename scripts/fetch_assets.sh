#!/usr/bin/env bash
# Fetch the reference material and target ROM used by SAGE.
#
#   references/model2.cpp, references/model2.h  MAME Model 2 driver (ROM_START)
#   references/supermodel/                      Model 3 emulator (architecture ref)
#   roms/doa.zip                                Dead or Alive (Sega Model 2B)
#
# The archive.org item stores doa.zip under a nested, space-containing path, so
# the download URL must be percent-encoded.
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

echo "==> Dead or Alive ROM set (doa.zip)"
doa_url="https://archive.org/download/m2emu1.1a/Sega%20Model%202%20Emu%201.1a%20and%20Full%20Romset/Model%202%20Romset%20%28Merged%29/doa.zip"
curl -fL --retry 3 -o "$roms/doa.zip" "$doa_url"

echo "==> Done"
ls -la "$roms/doa.zip"
