// Sega Model 2 video: palette, colour translation and the direct framebuffer.
//
// Ported from MAME's src/mame/sega/model2_v.cpp and src/mame/sega/segaic24.cpp
// (BSD-3-Clause, copyright-holders: Olivier Galibert, R. Belmont, ElSemi,
// Angelo Salese, David Haywood).
//
// The 3D polygon rasteriser is not implemented yet, so this renders the
// System 24 tile layers (HUD text, backgrounds) plus the direct framebuffer that
// the renderer test mode exposes. That is enough to prove the video path and to
// drive the SDL window; the rasteriser is the next milestone.

#include "sage/video.h"

#include <algorithm>
#include <cstring>

namespace sage {
namespace {

// System 24 tile generator geometry (segas24_tile_device::char_layout): 8x8
// tiles, 4 planes, planeoffset {0,1,2,3}, xoffset STEP8(0,4), yoffset
// STEP8(0,32), charincrement 8*32 = 256 bits (16 words) per tile. The decoded
// bit for pixel (x,y) plane p is at `code*256 + p + y*32 + x*4`, read LSB-first
// from the byte array; xormask 8 (see NATIVE_ENDIAN_VALUE_LE_BE) swaps the two
// bytes of each 16-bit char word relative to the i960's big-endian store.
constexpr int kTileBits = 8 * 32;
constexpr int kTilemapDim = 64;  // 64x64 tiles

// Palette / colour-translation layout (all byte offsets from 0x01800000).
constexpr std::size_t kPaletteOff = 0x0000;
constexpr std::size_t kColorXlatOff = 0x10000;

std::uint8_t gamma_lut(std::uint8_t v) {
    // model2_v.cpp: max((i - 64) * 255 / 191, 0)
    const int raw = (static_cast<int>(v) - 64) * 255 / 191;
    return static_cast<std::uint8_t>(std::max(raw, 0));
}

std::uint16_t rd16(const std::vector<std::uint8_t>& b, std::size_t off) {
    if (off + 1 >= b.size())
        return 0;
    return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

}  // namespace

Video::Video() { reset(); }

void Video::reset() {
    m_palram.assign(0x4000, 0);
    m_colorxlat.assign(0xc000, 0);
    m_char.assign(0x80000, 0);
    m_tile.assign(0x10000, 0);
    m_fb[0].assign(0x80000, 0);
    m_fb[1].assign(0x80000, 0);
    m_palette.assign(0x2000, 0xff000000u);
    m_rgba.assign(kWidth * kHeight, 0xff000000u);
    m_fb_bank = 0;
    m_palette_dirty = true;
    m_render_test_mode = false;
    m_videoctl = 0;
    m_xhout = 0;
    m_xvout = 0;
}

std::uint8_t* Video::map(std::uint32_t addr, std::size_t len) const {
    // System 24 tile generator, with MAME's mirroring: tile RAM is 64 KiB at
    // 0x01000000 mirrored by 0x110000; char RAM is 512 KiB at 0x01080000
    // mirrored by 0x100000.
    if (addr >= 0x01000000 && addr < 0x01020000) {
        const std::uint32_t off = addr - 0x01000000;
        if (off + len <= m_tile.size())
            return const_cast<std::uint8_t*>(m_tile.data()) + off;
    }
    if (addr >= 0x01100000 && addr < 0x01120000) {
        const std::uint32_t off = addr - 0x01100000;
        if (off + len <= m_tile.size())
            return const_cast<std::uint8_t*>(m_tile.data()) + off;
    }
    if (addr >= 0x01080000 && addr < 0x01100000) {
        const std::uint32_t off = addr - 0x01080000;
        if (off + len <= m_char.size())
            return const_cast<std::uint8_t*>(m_char.data()) + off;
    }
    if (addr >= 0x01180000 && addr < 0x01200000) {
        const std::uint32_t off = addr - 0x01180000;
        if (off + len <= m_char.size())
            return const_cast<std::uint8_t*>(m_char.data()) + off;
    }

    const auto region = [&](const std::vector<std::uint8_t>& buf, std::uint32_t base) -> std::uint8_t* {
        if (addr < base || addr - base + len > buf.size())
            return nullptr;
        return const_cast<std::uint8_t*>(buf.data()) + (addr - base);
    };
    if (auto* p = region(m_palram, 0x01800000)) return p;
    if (auto* p = region(m_colorxlat, 0x01810000)) return p;
    if (auto* p = region(m_fb[0], 0x11600000)) return p;
    if (auto* p = region(m_fb[1], 0x11680000)) return p;
    return nullptr;
}

bool Video::read(std::uint32_t addr, std::uint8_t& out) const {
    if (const std::uint8_t* p = map(addr, 1))
        out = *p;
    else
        out = 0;
    return true;
}

bool Video::write(std::uint32_t addr, std::uint8_t val) {
    std::uint8_t* p = map(addr, 1);
    if (!p)
        return false;
    *p = val;
    if (addr >= 0x01800000 && addr < 0x01804000)
        m_palette_dirty = true;
    return true;
}

std::uint32_t Video::palette_entry(int index) const {
    const std::uint16_t palcolor = rd16(m_palram, static_cast<std::size_t>(index) * 2);
    // MAME indexes m_colorxlat as a u16 array, so one intensity step is two
    // bytes; the byte offsets 0x80/0x4080/0x8080 are the >>1 bases doubled.
    const auto lookup = [&](std::size_t base, int shift) {
        const std::size_t off = base + static_cast<std::size_t>((palcolor >> shift) & 0x1f) * 0x200;
        return off < m_colorxlat.size() ? m_colorxlat[off] : 0;
    };
    const std::uint8_t r = gamma_lut(lookup(0x0080, 0));
    const std::uint8_t g = gamma_lut(lookup(0x4080, 5));
    const std::uint8_t b = gamma_lut(lookup(0x8080, 10));
    return 0xff000000u | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | b;
}

void Video::rebuild_palette() {
    for (int i = 0; i < 0x1000; ++i)
        m_palette[i] = palette_entry(i);
    m_palette_dirty = false;
}

// Decodes one tile pixel: 8x8, 4 planes. Plane p of pixel (x,y) lives at bit
// `code*256 + p + y*32 + x*4`, read LSB-first; bit order per MAME gfx_element.
int tile_pixel(const std::vector<std::uint8_t>& chram, int code, int x, int y) {
    const int base = (code & 0x3fff) * kTileBits;
    int pen = 0;
    for (int p = 0; p < 4; ++p) {
        const int b = base + p + y * 32 + x * 4;
        const std::size_t off = static_cast<std::size_t>(b >> 3);
        if (off >= chram.size())
            continue;
        if (chram[off] & (1 << (b & 7)))
            pen |= 1 << p;
    }
    return pen;
}

void Video::draw_tile_layers() {
    // MAME screen_update(): tilemaps 3 and 2 are drawn opaque, then 1 and 0
    // transparent. Tilemap L reads its map words at L*0x1000 and its scroll
    // registers from tile_ram[0x5000+L] / [0x5004+L] (word offsets).
    static constexpr int kOrder[4] = {3, 2, 1, 0};
    for (int idx = 0; idx < 4; ++idx) {
        const int layer = kOrder[idx];
        const bool opaque = layer >= 2;

        const std::uint16_t vscr_reg = rd16(m_tile, (0x5004 + layer) * 2);
        if (vscr_reg & 0x8000)  // layer disabled
            continue;
        const std::uint16_t hscr_reg = rd16(m_tile, (0x5000 + layer) * 2);
        const int hscr = (-static_cast<int>(hscr_reg)) & 0x1ff;
        const int vscr = static_cast<int>(vscr_reg) & 0x1ff;

        for (int ty = 0; ty < kTilemapDim; ++ty) {
            for (int tx = 0; tx < kTilemapDim; ++tx) {
                const std::uint16_t val =
                    rd16(m_tile, (static_cast<std::size_t>(layer) * 0x1000 +
                                  ty * kTilemapDim + tx) * 2);
                const int code = val & 0x3fff;
                const int color = (val >> 7) & 0xff;

                for (int py = 0; py < 8; ++py) {
                    const int my = (ty * 8 + py + vscr) & 0x1ff;
                    if (my >= kHeight)
                        continue;
                    for (int px = 0; px < 8; ++px) {
                        const int mx = (tx * 8 + px + hscr) & 0x1ff;
                        if (mx >= kWidth)
                            continue;
                        const int pen = tile_pixel(m_char, code, px, py);
                        if (pen == 0 && !opaque)
                            continue;
                        const int index = ((color << 4) | pen) & 0xfff;
                        m_rgba[static_cast<std::size_t>(my) * kWidth + mx] = m_palette[index];
                    }
                }
            }
        }
    }
}

void Video::draw_framebuffer() {
    // model2_v.cpp draw_framebuffer(): 512x400, colour 0 is transparent.
    const std::vector<std::uint8_t>& fb = m_fb[m_fb_bank];
    const int yoffs = (512 - 384);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t off =
                static_cast<std::size_t>(x + (y + yoffs) * 512) * 2;
            const std::uint16_t v = static_cast<std::uint16_t>(fb[off] | (fb[off + 1] << 8));
            const std::uint32_t r = gamma_lut(m_colorxlat[0x0080 + ((v >> 5) & 0x1f) * 0x200]);
            const std::uint32_t g = gamma_lut(m_colorxlat[0x4080 + ((v >> 10) & 0x1f) * 0x200]);
            const std::uint32_t b = gamma_lut(m_colorxlat[0x8080 + ((v >> 0) & 0x1f) * 0x200]);
            m_rgba[static_cast<std::size_t>(y) * kWidth + x] =
                0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

void Video::render() {
    if (m_palette_dirty)
        rebuild_palette();

    std::fill(m_rgba.begin(), m_rgba.end(), m_palette[0]);
    draw_tile_layers();
    if (m_render_test_mode)
        draw_framebuffer();
}

}  // namespace sage
