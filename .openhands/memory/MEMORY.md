# Project memory (SAGE)

Repo: `M5Devs/SAGE` — "Sega Arcade Golden-era Emulator", modern C++20 + CMake.
Target game: **Dead or Alive**, Sega **Model 2B** (i960 main CPU + 315-5881 GPU +
SCSP sound). `README.md`, `LICENSE` (GPL), and `.openhands/` were pre-existing.

## Build environment
- `cmake`/`ninja`/`unzip` are **not** preinstalled; install with
  `sudo -n apt-get update && sudo -n apt-get install -y cmake ninja-build unzip`
  (plain `apt-get` has no lock; `apt-get update` first or packages 404).
- g++ 14.2, CMake 3.31, 4 cores. `file` command is absent.
- miniz via FetchContent tag **3.1.2**; set `MINIZ_STANDALONE_PROJECT OFF` before
  `FetchContent_MakeAvailable` so it doesn't pull in its examples/tests.

## ROM source + quirks (learned the hard way)
- `roms/doa.zip` from Internet Archive item `m2emu1.1a` lives at a **nested,
  space-containing path**:
  `.../m2emu1.1a/Sega Model 2 Emu 1.1a and Full Romset/Model 2 Romset (Merged)/doa.zip`.
  Must be percent-encoded or the download 404s. md5 `1fcf21db...6bfd`.
- The archive is actually the MAME **`doab` (Rev B, Dec 6 1996)** set, *not* the
  default `doa` (Rev C). `doa` and `doab` differ **only** in the two main-CPU
  program ROMs (`epr-19379*.15`, `epr-19380*.16`). Loader auto-detects by CRC.
- **Resolve ROMs by CRC32 first, then filename.** The zip stores the sound ROM
  as `epr-19328.30` (MAME: `.31`) and the sample ROMs as `mpr-19329.31` ... `.34`
  (MAME: `.32` ... `.35`).
- `mpr-19324.19` in the zip is `D972201F`, which MAME does **not** list (MAME's
  `doab` wants `0D6BF454`). The m2emu 1.1a DAT documents `D972201F`, so treat it
  as a known alternate dump (data row `alt_crc`), not a loader failure.
- MAME load macro semantics (from `src/emu/romentry.h`, not romload.h):
  `ROM_LOAD32_WORD` = group 2 / skip 2 / no reverse;
  `ROM_LOAD16_WORD_SWAP` = group 2 / reverse;
  `ROM_LOAD32_WORD_SWAP` = group 2 / skip 2 / reverse.
  Placement: `dst = off + k*(group+skip) + (rev ? group-1-j : j)`.
- After loading, MAME `region_post_process()` **byte-swaps non-native regions**
  (host is LE): only `samples` (`ROM_REGION16_BE`) is affected here.
- Region sizes: maincpu 2MiB, main_data 32MiB, polygons 32MiB (ERASEFF),
  textures 8MiB, copro_data 8MiB (ERASE00, all-zero), audiocpu 512KiB,
  samples 8MiB. `ROM_COPY` duplicates main_data 0x1800000->0x1C00000 (4MiB).
- Golden region CRC32s (doab semantics, archive files): maincpu `E64DCBF3`,
  main_data `3EFB6628`, polygons `939D5D9E`, textures `B17541BD`,
  copro_data `1AD2BC45`, audiocpu `31D118A5`, samples `DADF9335`.
- Supermodel (`references/supermodel`) is **Model 3**, so it has no DOA ROM
  tables — it's an architecture reference only.

## Conventions
- `.gitignore`: `build/`, `references/`, `roms/`, `*.zip`.
- `scripts/fetch_assets.sh` re-downloads references + doa.zip; exposed as the
  optional CMake target `fetch-assets` (not part of the default build).
- `.openhands/memory/legacy-openrom-memory.md` is stale content from an unrelated
  OpenROM Marketplace project that previously occupied this path; ignore it.

## i960 CPU core + Bus (Task 2, 2026-11-05)
- `src/cpu/i960.cpp` + `i960dis.cpp` are standalone ports of MAME
  `src/devices/cpu/i960/{i960,i960dis}.cpp` (BSD-3-Clause, Farfetch'd / R.
  Belmont). Framework deps removed: `memory_access<>` -> `cpu::BusInterface`,
  `fatalerror` -> `cpu::Fault` exception, `save_item`/`state_add` dropped,
  `util::stream_format` -> local `fmt()`.
- **Critical porting bug to remember**: MAME's `util::sext(v, bits)` must MASK to
  the low `bits` bits before sign-extending. Writing it as `(v ^ m) - m` without
  the mask silently corrupts `get_disp_s` (13-bit COBR branch displacement) and
  makes the CPU jump to garbage (IP became 0x3DA44420 instead of 0x420).
- MAME's `m_icount` is a *remaining budget*; `execute_op` DECREMENTS it. The
  standalone `step()` sets `m_icount = 1` then returns `1 - m_icount` (so a
  `-9` call yields 10 cycles, matching `execute_min_cycles()==1`).
- `BusInterface` reads are `const` (the disassembler takes a `const BusInterface&`);
  the Bus diagnostic counters are therefore `mutable`.
- `BusInterface::burst(addr)` mirrors MAME's per-map-entry
  `flags(i960_cpu_device::BURST)`: ROM/RAM true, register/FIFO apertures false.
  Multi-word loads (`ldl`/`ldt`/`ldq`) only advance the address on BURST.
- Model 2B map implemented (subset of `model2b_0229_mem`): ROM 0x0, work RAM
  0x00200000 & 0x00500000, geo 0x00800000, buffer RAM 0x00900000
  (0x80000-mirror), CPU ctl 0x00E00000 (0x38), IRQ ctl 0x00E80000, timers
  0x00F00000, palette 0x01800000, backup 0x01D00000, main_data ROM
  0x02000000 & 0x06000000, texture RAM 0x11000000/0x11200000
  (0x100000-mirror), luma 0x11400000.
- **Boot is real**: reset reads SAT=0, PRCB=0xB0, entry IP=0x410. The first
  block (0x410-0x458) copies a 26-entry dword table from ROM 0x3D0 to the CPU
  control block at 0xE00000; IP then walks 0x634 -> 0x668 (clearing 32MiB of
  work RAM) -> 0x724C. 6M instructions reaches 0x724C with a few hundred
  thousand unmapped reads (SHARC/uart/comm apertures not yet modelled).
- `main.cpp` takes an optional 3rd arg = instruction count (default 1000) for
  longer soak runs: `./build/sage roms/doa.zip doa 5000000`.

## Task 3: video/boot findings (2026-11-05)
- **doa is a `model2b_0229` set** (317-0229 protection), NOT the SHARC `5881`
  variant. The apparent boot hang is the intended per-frame VBLANK wait, not a
  stall: the game is alive and rendering.
- **The game boots to its own diagnostic screen**: "Motion Initialize Err!!
  ERR ANIME = 0x00B0 / real anime name exit on ACODE.DEF / PUSH H or P or K!!
  then NEXT ANIME Initialize!!". This is because the **SHARC/ADSP-21062 copro is
  stubbed** (the 3D/geometry + motion engine never responds), so this is a
  correct-looking, honest emulator state, not a video bug.
- The tile layer decodes as ASCII in char RAM: `tilemap word = charcode |
  (color<<7) | (0x8000 = category)`, so a rendered row of tile values is
  literally the on-screen text. Decoding layer 0 (0x0000 offset) as 64x64
  `chr(v&0xff)` is the fastest way to read what the game is printing.
- **colorxlat stride is 0x200 bytes** (MAME `m_colorxlat` is `u16[]`; byte
  offsets 0x80/0x4080/0x8080 are the >>1 bases doubled). The earlier 0x100
  stride collapsed the palette. Palette resolves through
  `m_colorxlat[base + ((pal>>shift)&0x1f)*0x200]` then a gamma LUT
  `max((i-64)*255/191, 0)`.
- `Video::map()` mirrors MAME: tile RAM 0x01000000 (mirror 0x110000), char RAM
  0x01080000 (mirror 0x100000). Palette 0x01800000, colorxlat 0x01810000.
- doa's loader verifies 22/23 ICs; `mpr-19324.19` is a known alternate dump
  (D972201F vs MAME's 0D6BF454) — reported as "OK (alt dump)", not a failure.
- Debug env hooks that proved useful: `SAGE_TRACE`, `SAGE_WATCH=lo:hi`
  (+`SAGE_WATCH_R`), `SAGE_DISASM=hex:count`, `SAGE_DUMPVID`, `SAGE_DUMPRAW`,
  `SAGE_PCORDER`, `SAGE_INTTAB`, `SAGE_PPM`, `SAGE_HEADLESS`, `SAGE_REALTIME`.
- `build-rel`/`build` are git-ignored; the SDL build needs
  `LD_LIBRARY_PATH=/opt/sdl2/usr/lib/x86_64-linux-gnu`.
