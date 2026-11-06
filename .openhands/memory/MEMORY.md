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

## SAGE emulator bring-up (2026-11-05)
- `scripts/fetch_assets.sh` pulls the MAME `model2.cpp/.h`, a shallow Supermodel
  clone, and `doa.zip` (the archive.org path is nested + space-containing, so the
  URL must be percent-encoded). `roms/`, `references/`, `build/`, `*.zip` are all
  git-ignored.
- **SHARC must start HALTED.** MAME's `model2b_state::machine_reset()` asserts
  HALT on the ADSP21062 and only clears it from `copro_ctl1_w()`. Starting the
  SHARC un-halted makes it fetch garbage before the i960 uploads the program
  (PC ran off to 0x5AE24FEA). With the halt the boot lands exactly on PC=0x20004
  and then sits in its idle loop at 0x20123 — correct.
- MAME i960 device handlers receive **word offsets** (`byte >> 2`), so the copro
  IOP window needs `(addr - kCoproIopBase) >> 2`.
- The i960 main loop at 0x27E0 spins on a VBLANK semaphore at 0x54B150; the
  IRQ0 handler at 0x7700/0x7838 sets it and the main loop clears it — this is
  normal frame sync, NOT a hang. IRQ taken count tracks VBLANK count.
- **SDL2 was absent and there is no `/opt/sdl2`.** It can be installed with
  `sudo apt-get install libsdl2-dev` after `apt-get update` (the snapshot.debian.org
  mirror 503/504s intermittently — retry with `--fix-missing`). `Display::open()`
  now falls back to `SDL_RENDERER_SOFTWARE` when no GPU is present, so
  `SDL_VIDEODRIVER=dummy` opens a window.
- Video is still essentially black (2 distinct colors: black bg + a white
  overlay) — tile/char/colorxlat/palette path is a separate, unfinished task.

## DOA "Motion Initialize Err" root cause (2026-11-05, fixed)
- **Root cause**: SAGE aliased i960 window `0x06000000` to `main_data[0]`.
  MAME's `model2_base_mem` maps it to `main_data[0x1000000]` (the "extra data"
  ROMs). DOA reads its animation tables from `0x06000000`, so the wrong offset
  made the game abort with `Motion Initialize Err!! ERR ANIME = 0x00B0` and park
  on the diagnostic screen forever (SHARC idle loop at `0x020123` was a red herring
  — that is the correct wait-for-command spin; `0x20122` is a `DO ... UNTIL` loop).
  Fixed in `Bus::map()`; the diagnostic error text disappeared immediately and the
  game now reads `0x06000000` (34k+ reads) and renders its font/logo tiles.
- **Second bug**: 317-0229 protection registers (`0x01d87ff0-0x01d87ffb`, status
  `0x01d8400c`) overlay the tail of the protection source-RAM window
  (`0x01d80000-0x01d87fff`). `read16/32` and `write16/32` must decode the register
  block **before** the generic `map()` RAM fallback, else the registers are dead
  code (reads return raw RAM). Matches MAME `model2_0229_mem` (specific handler wins).
- DOA never actually polls the protection status during boot, so that fix alone did
  not change the symptom — but it is still correct for MAME parity.
- Remaining gap to visible gameplay: the 3D polygon rasteriser is not implemented
  (only System 24 tile layers + direct framebuffer render), and the game still
  does not issue SHARC commands (0 non-upload `fifo_w`) — copro command path is the
  next milestone. Screen shows the game's own tile/logo output now, not the error.
- Debug hooks added: `SAGE_SHARC_PC` (PM PC histogram), `SAGE_SHARC_DUMP=base:count`
  (PM 48-bit words around PC), `SAGE_SHARC_EXT`, `SAGE_PROT_TRACE` (removed).

## TASK 6 root cause: no 3D because the GEO path is entirely unwired (2026-11-05)
- DOA pushes ~55k dwords/frame into the GEO **program FIFO** at `0x00804000`
  (`model2b_state::geo_prg_w`) and toggles `m_geo_write/read_start` between 0x0 and
  0x10000 via `0x00801008`/`0x00803008` (double buffering). But `Bus::map()`
  mapped `0x00800000-0x00803fff` and `0x00804000-0x00807fff` to plain RAM
  (`m_geo`, `m_geo_prg`) **before** the register handlers, so `geo_write()`
  and `geo_prg_w()` were dead code: `m_bufferram` was never filled and
  `geo_parse()` was never called. SAGE's `trigger_vblank()` only raised IRQ0.
- Fix: unshadow the two GEO windows (route them to a new `Geometry` module),
  port the geometry engine + rasterizer, and call `geo_parse()` at vblank.
- `pz`/`pu`/`pv` are macros: `p[0]`/`p[1]`/`p[2]` of `poly_manager::vertex_t`
  (MaxParams=4). `raster_state` is heap-allocated in MAME (make_unique) — the
  poly_list is 32768 * ~180B, so keep it off the stack (use vectors).
- MAME screen_update order: tiles {3,2} opaque -> tiles {1,0} transparent ->
  3D polygons -> tiles {(layer<<1)|1} sprites. Tilemap layer L reads map words at
  L*0x1000; scroll regs tile_ram[0x5000+L]/[0x5004+L] (word offsets).
- DOA H/V sync: `0x01040000`=0xFFA4 -> crtc_xoffset = 84-92 = -8;
  `0x01060000`=0xFFD8 -> crtc_yoffset = 130-40 = 90. Renderer defaults are
  xoffs=90, yoffs=-8 (differ from crtc until the game writes them).

## TASK 6 / T4 root cause: 3D is black because the GEO engine is never booted (2026-11-05)
- Instrumented trace of `geo_prg_write()` (0x00804000 FIFO) shows the i960 only ever
  pushes **zero dwords** with `geoctl == 0x80000000` (geo *upload* mode). `set_geoctl`
  never sees the high bit clear, so `geo_prg_write` never calls `push_geo_data`, and
  `m_bufferram` keeps its reset pattern `0x07800F0F` (= GEO "end" opcode, function 0x0F).
- Consequence: `geo_parse()` runs but immediately hits the init pattern and ends with
  0 polygons. The earlier "407k polygons rendered" figure came from `render_one()`
  being invoked on stale/garbage polygon entries, not real DOA geometry.
- So the black 3D is **not** a viewport/clip bug. It is the same root cause already
  recorded for the copro: the i960 stalls before it can hand the geometry list to the
  SHARC, which is the Model 2B path that actually feeds the GEO engine. Fixing it means
  making the SHARC copro command path run (upload program, exchange FIFO commands) so
  the i960 boots geo (`geoctl` high bit cleared) and writes the real display list.
- `m_xoffs=90 / m_yoffs=-8` renderer defaults and `crtc_xoffset`/`crtc_yoffset` from
  `0x01040000`/`0x01060000` are correct; `poly.viewport[]` parsed from window data
  (0x0003) is correct (`vpraw=[8,90,504,474]`).

## T2 root cause + fix: i960/SHARC FIFO flow control (2026-11-05)
- **Symptom**: i960 raced ~100k words ahead of the SHARC; SHARC PC diverged to
  `0x817FCFF1` (bogus dispatch from empty-FIFO garbage), then a hard hang/timeout
  around frame 545 (600 frames never finished).
- **Cause**: SAGE's copro FIFOs were unbounded deques returning 0 on empty reads.
  MAME's `generic_fifo_u32_device` instead *couples the two CPUs*:
  - output FIFO: `pop()` on empty → `m_on_fifo_empty_pre_sync` = `i960_stall()`
    (stalls the i960 for one instruction, retried next timeslice);
  - input FIFO (cap 16): `push()` when full → `m_on_fifo_full_post_sync` =
    `set_input_line(HALT, ASSERT)` on the i960.
  Model2B `machine_start()` (`references/model2.cpp` ~line 241) wires exactly this
  with `setup(16, ...)` for both FIFOs.
- **Fix** (`src/bus.cpp`): `copro_fifo_sync(bool need_out)` runs the SHARC forward
  (64-cycle slices, 4000-iter cap) until the threshold is satisfied. Called from
  `copro_fifo_r` (empty out), `copro_fifo_w` (full in) and `copro_function_port_w`.
  Key detail: **early-out when `need_out && fifo_in.empty() && sharc.pc()` is
  unchanged** — the SHARC is parked on an input command that has not been pushed,
  so spinning can never produce output. Without this the loop burned the full
  4000 iterations and the run took 10+ min for 600 frames.
- **Verified**: SHARC idles correctly at `PC=0x020123` (its real wait loop), 0
  invalid dispatches, 0 empty-FIFO output reads, 1800 frames (30 s emulated) clean.
- Remaining (separate issue, NOT flow control): the i960 parks in its delay loop
  at `0x27E0`/`0x27E8` (`ld 0x54b150,g4; cmpibne`) after ~frame 150. `0x54b150` is
  written by `0x7838` and `0x27FC`, so it is being serviced; 3D stays black because
  `geoctl` is never cleared (GEO never booted) — see TASK 6 note above.
- Debug env gates used while chasing this (kept in tree): `SAGE_SHARC_TRACE`,
  `SAGE_WAITDBG`, `SAGE_SHARC_RING`, `SAGE_SHARC_WIN`, `SAGE_SHARC_BADPC`,
  `SAGE_SHARC_LOOPDBG`, `SAGE_SHARC_CMD`, `SAGE_SHARC_DM`, `SAGE_SHARC_UNMAP`,
  `SAGE_DISASM=<hex[:n],...>` (prints live disassembly at boot).

## 0-polygon root cause: i960 BURST flag on ROM (2026-11-05)
- **Symptom**: `GEO parses: N polygons rendered: 0` — the display list parsed but
  every `object_data` (fn 01) submitted the SAME word 4× as tpa/tha/oba/obc, so
  `obp` pointed at garbage and no polygon ever reached the rasteriser.
- **Root cause**: `Bus::burst()` returned true only for *writable* memory
  (`map(...) && w`). The i960 core's `ldq`/`ldt`/`ldl`/`stq` advance the address
  only when the target reports BURST (`if (flags & kBurst) t1 += 4;`). MAME flags
  the `main_data` ROM region (and maincpu ROM, workram, bufferram, geo_prg,
  copro_function, texture/luma/palette/backup RAM) with
  `i960_cpu_device::BURST`. Without it, `ldq (g5),g4` at PC 0x65CD4 read
  main_data 0x0207A470 four times instead of the real quad
  `002142D8 00944350 00004929 0021000F` → 4 identical operands.
- **Fix** (`src/bus.cpp::burst`): return true for every region MAME flags BURST.
  Practically: `map(addr,4,w) != nullptr` (drop the `&& w`), plus explicit true
  for geo_prg (0x00804000) and copro_function (0x00880000) which have no `map`
  entry, and explicit **false** for the CPU wait-state block 0x00e00000
  (`kCpuCtlBase`), the one plain-RAM aperture MAME leaves non-BURST.
- **Result**: 80677 polygons rendered over 1300 frames; frame dumps show the DOA
  title screen + 3D content (36 unique colours, no longer all-black).
- **Second fix** (`Bus::map`): MAME maps 0x00200000-0x0021ffff as RAM and
  0x00220000-0x0023ffff as **maincpu ROM at +0x20000** (`model2_5881_mem`); SAGE
  had the whole 256 KiB as RAM. Now split accordingly (read-only upper half).
- **How to verify**: `SAGE_HEADLESS=1 ./build/sage roms/doa.zip doa 1300` → expect
  `polygons rendered` in the tens of thousands, not 0. (Polygons start rendering
  ~frame 1050 when the game first uploads its display list; earlier frames are
  legitimately empty because geoctl/boot hasn't happened yet.)
- **Method that found it**: diff SAGE's memory map + `burst()` semantics against
  MAME (`references/model2.cpp` map + `references/i960/i960.cpp` ldq/stq). No
  opcode hunting — the reference cores had the answer.

## Git push auth quirk (2026-11-05)
- The remote's embedded credential (`https://ghu_...@github.com/M5Devs/SAGE.git`)
  is **stale** — `git push origin main` hangs on an interactive password prompt
  (and there is no TTY, so it just times out).
- Working push: `git -c credential.helper= push
  "https://x-access-token:${GITHUB_TOKEN}@github.com/M5Devs/SAGE.git" main`
  (inline, no persisted credential). `GITHUB_TOKEN` is injected from secrets.
- `M5Devs/SAGE` is **public**, default branch `main`; raw files are reachable at
  `https://raw.githubusercontent.com/M5Devs/SAGE/main/<path>`.
- Screenshots live in `docs/screenshots/` (PPM→PNG via Pillow, 2× NEAREST).
