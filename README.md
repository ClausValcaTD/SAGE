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
│   └── types.h             Region / RomVerify / LoadReport
├── src/
│   ├── main.cpp            CLI + status tables
│   └── rom_loader.cpp      ROM table + load/verify implementation
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
./build/sage                    # optionally: ./build/sage <zip> <doa|doab>
```

The loader prints each ROM IC with its verified CRC32, the mapped memory
regions, and the program memory range.

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
