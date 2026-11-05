# SAGE — Sega Arcade Golden-era Emulator

A modern C++20 emulator project. The first target is **Dead or Alive** on Sega
**Model 2B** hardware (i960 main CPU + 315-5881 GPU + SCSP sound).

This initial milestone implements the **ROM loader**: it parses a MAME-compatible
`doa.zip` and assembles the game's memory regions exactly the way MAME's
`ROM_START( doa )` / `ROM_START( doab )` in `src/mame/sega/model2.cpp` describe.

## Layout

```
SAGE/
├── CMakeLists.txt          C++20 build, FetchContent miniz
├── include/sage/
│   ├── rom_loader.h        RomLoader interface
│   ├── types.h             Region / RomVerify / LoadReport
│   ├── bus.h               Model 2B memory interconnect
│   └── cpu/i960.h          standalone i960 CPU + disassembler
├── src/
│   ├── main.cpp            CLI, status tables, boot trace
│   ├── rom_loader.cpp      ROM table + load/verify implementation
│   ├── bus.cpp             address decoding + RAM/ROM
│   └── cpu/
│       ├── i960.cpp        instruction interpreter
│       └── i960dis.cpp     instruction disassembler
├── scripts/fetch_assets.sh downloads references + doa.zip
├── references/             MAME sources + Supermodel (git-ignored)
└── roms/                   doa.zip (git-ignored)
```

## Fetching assets

```bash
./scripts/fetch_assets.sh      # or: cmake --build build --target fetch-assets
```

This pulls the MAME Model 2 driver sources, a shallow clone of Supermodel
(architecture reference), and `roms/doa.zip` from the Internet Archive.

## Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/sage                          # optionally: ./build/sage <zip> <doa|doab> [instructions]
```

The loader prints each ROM IC with its verified CRC32, the mapped memory
regions, and the program memory range. It then resets the i960, prints the boot
vector (SAT/PRCB/IP read from 0x00000000), and steps the CPU for 1000
instructions with a disassembled trace of the first 20 and a checkpoint every
200.

## CPU core

`src/cpu/i960.cpp` is a standalone port of MAME's `src/devices/cpu/i960` core
(BSD-3-Clause, Farfetch'd / R. Belmont) with the MAME device framework removed:
the `memory_access<>` cache is replaced by a `BusInterface`, `fatalerror()` by a
recoverable `Fault` exception, and state registration / `save_item` dropped.
Instruction semantics are kept verbatim so traces match MAME's.

`Bus` (src/bus.cpp) decodes the subset of `model2b_0229_mem()` the boot path
touches: program ROM at 0x00000000, `main_data` ROM at 0x02000000/0x06000000,
work RAM, buffer/palette/texture/luma RAM, and the CPU-control register block at
0x00e00000. Unmapped accesses are counted and return 0 rather than faulting.

## Notes

- ROM files are resolved inside the archive **by CRC32 first**, then by
  filename. The archive's sound ROM and two sample ROMs are stored under labels
  that differ from MAME's, so CRC matching is required.
- The Internet Archive `doa.zip` contains the **Revision B** program ROMs, so the
  loader auto-detects the revision and reports it.
- `mpr-19324.19` is present as dump `D972201F` (documented in the m2emu 1.1a DAT),
  which is accepted as a known alternate of MAME's `0D6BF454`.
- Region contents are byte-swapped after loading, matching MAME's
  `region_post_process()` (visible in the big-endian `samples` region).
