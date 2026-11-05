#pragma once

#include <cstdint>
#include <vector>

namespace sage {

// Sega Model 2 video subsystem: palette / colour-translation RAM, the System 24
// tile generator (char + tile RAM) and the direct framebuffer. It decodes the
// video apertures of MAME's model2_base_mem()/model2b_crx_mem() and renders the
// visible 496x384 area into an RGBA buffer for the SDL display.
//
// The 3D polygon rasteriser is out of scope; the direct framebuffer and the
// System 24 tile layers are what this class produces.
class Video {
public:
    static constexpr int kWidth = 496;
    static constexpr int kHeight = 384;

    Video();
    void reset();

    // Byte-level access. Returns false when `addr` is not a video aperture.
    bool read(std::uint32_t addr, std::uint8_t& out) const;
    bool write(std::uint32_t addr, std::uint8_t val);

    // Fast path for the bus: writable pointer into the backing store for a
    // multi-byte access wholly inside one video aperture, else nullptr.
    std::uint8_t* map(std::uint32_t addr, std::size_t len) const;

    // Called by the bus after a store into palette RAM.
    void mark_palette_dirty() { m_palette_dirty = true; }

    // Rebuilds the RGBA frame from the current palette, tile and framebuffer RAM.
    void render();

    const std::uint32_t* rgba() const { return m_rgba.data(); }

    // Debug accessors.
    const std::vector<std::uint8_t>& palram() const { return m_palram; }
    const std::vector<std::uint8_t>& colorxlat() const { return m_colorxlat; }
    const std::vector<std::uint32_t>& palette() const { return m_palette; }
    const std::vector<std::uint8_t>& charram() const { return m_char; }
    const std::vector<std::uint8_t>& tileram() const { return m_tile; }

    // Selects which direct framebuffer (A/B) is scanned out.
    void set_framebuffer_bank(int bank) { m_fb_bank = bank & 1; }
    void set_render_test_mode(bool on) { m_render_test_mode = on; }
    bool render_test_mode() const { return m_render_test_mode; }

    std::uint16_t video_control() const { return m_videoctl; }

private:
    void rebuild_palette();
    std::uint32_t palette_entry(int index) const;
    void draw_tile_layers();
    void draw_framebuffer();

    std::vector<std::uint8_t> m_palram;     // 0x01800000, 0x4000 bytes
    std::vector<std::uint8_t> m_colorxlat;  // 0x01810000, 0xc000 bytes
    std::vector<std::uint8_t> m_char;       // 0x01080000, 0x80000 bytes
    std::vector<std::uint8_t> m_tile;       // 0x01000000, 0x10000 bytes
    std::vector<std::uint8_t> m_fb[2];      // 0x11600000 / 0x11680000, 0x80000 bytes
    std::vector<std::uint32_t> m_palette;   // 0x2000 RGBA entries
    std::vector<std::uint32_t> m_rgba;      // kWidth * kHeight

    int m_fb_bank = 0;
    bool m_palette_dirty = true;
    bool m_render_test_mode = false;
    std::uint16_t m_videoctl = 0;
    std::uint16_t m_xhout = 0;
    std::uint16_t m_xvout = 0;
};

}  // namespace sage
