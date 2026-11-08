# 🗺️ SAGE Architecture Roadmap
**SAGE: Sega Arcade Golden-era Emulator**  
*A modern, standalone, portable C++20 emulator preserving Sega's legendary 3D arcade boards (Model 2, Model 3, and beyond).*

---

## 📍 Phase 1: The Core Foundation (Model 2B Boot & Proof of Life)
- [x] **Milestone 1: ROM Loader & Memory Architecture**
  - MAME-accurate ROM loading, CRC32 verification, auto-detection of revisions (`doab`).
  - Automatic de-interleaving / byte-swizzling and memory mapping (90.5 MB).
- [x] **Milestone 2: Intel i960 Main CPU Core**
  - Decouple MAME's i960 interpreter into standalone modern C++20.
  - Implement system bus interconnect and PRCB boot vector execution.
  - Fix 13-bit sign-extension branch displacement masking.
- [x] **Milestone 3: Hardware Peripherals & Frame Loop**
  - Implement Serial EEPROM 93C46 (NVRAM / configuration storage).
  - VBLANK IRQ0 generation and stable 60 Hz frame synchronization.
  - Sega custom security chip (317-0229) boot handshake.
  - SDL2 display window initialization (496x384 native arcade resolution).
- [x] **Milestone 4: ADSP-21062 SHARC DSP Coprocessor**
  - Transpile MAME's SHARC DSP core via automated Python tooling (`gen_sharc.py`).
  - Implement 48-bit PM / 32-bit DM 3:2 packed SRAM model.
  - Address shift (`byte >> 2`) and HALT boot sequencing fixes.
  - Resolve the `ERR ANIME = 0x00B0` motion initialization barrier.

---

## 🎨 Phase 2: Complete Video Pipeline & Presentation
- [x] **Milestone 5: 2D Video Engine & Palette Decoding**
  - Palette RAM decoding from raw 16-bit Sega BGR to 32-bit RGBA.
  - Connect Color Translation (`colorxlat`) lookup table.
  - System 24 tilemap planes (Text, Foreground, Background) decoding with correct 16-word stride.
- [x] **Milestone 6: 3D Polygon Rasterization Engine**
  - Parse Model 2 3D geometry display lists and vertex buffers.
  - Software scanline rasterizer with perspective-correct interpolation and Z-buffering.
  - Texture decal mapping from the `textures` ROM region: texture page, palette/luma
    lookups, and bilinear filtering with mipmaps. Luma RAM is decoded as a
    half-word-packed array (`umask16(0x00ff)`), so character faces, costume
    patterns, and arena walls render textured rather than flat-shaded.
  - Render full 3D characters, arenas, and lighting at 60 FPS.

---

## 🔊 Phase 3: Sound System & Cabinet Inputs
- [x] **Milestone 7: Sega SCSP Sound Hardware**
  - Motorola 68EC000 sound CPU core integration.
  - Yamaha YMF292-F (SCSP) PCM/FM audio synthesis and DSP effects.
  - Synchronization between main CPU, sound CPU, and audio buffer.
- [ ] **Milestone 8: Arcade Input System & QoL**
  - Gamepad & Arcade stick support via SDL2/SDL3 controller subsystem.
  - Native Test/Service mode toggle (F2/F1 keys) for cabinet configuration.
  - Multi-Mouse / RawInput lightgun support (independent P1 and P2 mouse/lightgun assignment).
  - Built-in CRT lightgun flash suppression (No more blinding white flashes in House of the Dead).

---

## 🌟 Phase 4: Enhancements & Community Features
- [ ] **Milestone 9: Lua Scripting & Cheats Engine**
  - Embedded lightweight Lua runtime.
  - Memory read/write hooks for community modding and TAS.
  - Native 16:9 / 21:9 Widescreen camera geometry scripts.
- [ ] **Milestone 10: RetroAchievements Integration (`rcheevos`)**
  - Embed official ANSI C `rcheevos` library.
  - Memory inspection callbacks for Model 2 achievement triggers and leaderboards.
  - First-ever RetroAchievements support for Sega Model 2 games!

---

## 🚀 Phase 5: Library Expansion (The Complete Sega 3D Era)
- [ ] **Milestone 11: Sega Model 2A & 2C Support**
  - Fujitsu TGP (MB86234 / MB86235) math coprocessor integration.
  - Support for *Daytona USA*, *Virtua Cop 1 & 2*, *Virtua Fighter 2*, *Sega Rally*, and *The House of the Dead*.
- [ ] **Milestone 12: Sega Model 3 & Supermodel Synthesis**
  - Integrate Model 3 architecture (PowerPC 603e + Real3D graphics) from Supermodel.
  - Support for *Daytona USA 2*, *Scud Race*, and *Star Wars Trilogy Arcade*.

---

## 📱 Phase 6: Cross-Platform & Next-Gen Reach
- [ ] **Milestone 13: Android & Retro Handhelds**
  - Native ARM64 build target.
  - Touchscreen overlays and controller auto-binding for Android devices (Odin, Retroid, Anbernic).
- [ ] **Milestone 14: RetroArch / Libretro Core (`sage_libretro`)**
  - Expose clean Libretro C API.
  - Cross-platform core availability on Steam Deck, Linux, Windows, macOS, and iOS.
- [ ] **Milestone 15: Modern Online Rollback Netplay**
  - Low-latency peer-to-peer rollback netcode for fighting and racing games.
