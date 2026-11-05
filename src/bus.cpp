// Sega Model 2B memory map and peripheral integration.
//
// Ported from MAME's src/mame/sega/model2.cpp (model2_base_mem,
// model2b_crx_mem, model2_0229_mem), src/mame/sega/315_5649.cpp and
// src/mame/sega/315-5838_317-0229_comp.cpp (BSD-3-Clause).
//
// Register windows use the same decoding MAME does: the handler offset is the
// word index `(addr - base) >> 2` and `umask32(0x00ff00ff)` connects only byte
// lanes 0 and 2, so a 32-bit access at register N covers registers N and N+1.

#include "sage/bus.h"

#include <cstdio>

namespace sage {
namespace {

constexpr std::uint32_t kRomBase = 0x00000000;
constexpr std::uint32_t kRam0020Base = 0x00200000;
constexpr std::uint32_t kWorkRamBase = 0x00500000;
constexpr std::uint32_t kGeoBase = 0x00800000;
constexpr std::uint32_t kGeoPrgBase = 0x00804000;
constexpr std::uint32_t kCoproFunctionBase = 0x00880000;
constexpr std::uint32_t kCoproFifoBase = 0x00884000;
constexpr std::uint32_t kCoproIopBase = 0x008c0000;
constexpr std::uint32_t kBufferRamBase = 0x00900000;
constexpr std::uint32_t kCoproCtlBase = 0x00980000;
constexpr std::uint32_t kUartBase = 0x009c0000;
constexpr std::uint32_t kCpuCtlBase = 0x00e00000;
constexpr std::uint32_t kIrqBase = 0x00e80000;
constexpr std::uint32_t kTimersBase = 0x00f00000;
constexpr std::uint32_t kRenderModeBase = 0x10000000;
constexpr std::uint32_t kPolygonCountBase = 0x10400000;
constexpr std::uint32_t kTexture0Base = 0x11000000;
constexpr std::uint32_t kTexture1Base = 0x11200000;
constexpr std::uint32_t kLumaBase = 0x11400000;
constexpr std::uint32_t kIoBase = 0x01c00000;
constexpr std::uint32_t kBackupBase = 0x01d00000;
constexpr std::uint32_t kProtBase = 0x01d80000;
constexpr std::uint32_t kMainDataBase = 0x02000000;
constexpr std::uint32_t kMainDataBase2 = 0x06000000;

constexpr std::size_t kRam0020Size = 0x00040000;
constexpr std::size_t kWorkRamSize = 0x00100000;
constexpr std::size_t kBufferRamSize = 0x00020000;  // mirrored across 0x80000
constexpr std::size_t kBackupSize = 0x00004000;
constexpr std::size_t kGeoSize = 0x00004000;
constexpr std::size_t kGeoPrgSize = 0x00004000;
constexpr std::size_t kCpuCtlSize = 0x00000038;
constexpr std::size_t kProtSize = 0x00008000;
constexpr std::size_t kTextureSize = 0x00100000;  // mirrored across 0x100000

// Write-only model2b registers that MAME maps as `.nopw()`: the ABSEL register,
// the System 24 H/V sync outputs, the video sync switch and the 3D z-clip port.
bool is_nopw(std::uint32_t addr) {
    return (addr >= 0x01020000 && addr < 0x01080000) ||
           (addr >= 0x01120000 && addr < 0x01180000) ||
           (addr >= 0x0181c000 && addr < 0x0181c004);
}

bool in_range(std::uint32_t addr, std::size_t len, std::uint32_t base, std::size_t size) {
    return addr >= base && static_cast<std::uint64_t>(addr - base) + len <= size;
}

// `span` is the mirror period; the mapped store is `size` bytes repeated across it.
bool in_mirror(std::uint32_t addr, std::size_t len, std::uint32_t base, std::size_t size,
               std::size_t span) {
    if (addr < base || addr - base >= span)
        return false;
    return (addr - base) % span + len <= size;
}

std::uint8_t byte_lane(std::uint32_t v, unsigned lane) {
    return static_cast<std::uint8_t>(v >> (lane * 8));
}

}  // namespace

void Bus::attach(const RomLoader& roms) {
    m_roms = &roms;

    if (const Region* cpu = roms.region("maincpu")) {
        m_rom = cpu->data.data();
        m_rom_size = cpu->data.size();
    }

    m_workram.assign(kWorkRamSize, 0);
    m_ram_0020.assign(kRam0020Size, 0);
    m_backup.assign(kBackupSize, 0xff);  // NVRAM DEFAULT_ALL_1
    m_bufferram.assign(kBufferRamSize, 0);
    m_geo.assign(kGeoSize, 0);
    m_geo_prg.assign(kGeoPrgSize, 0);
    m_cpu_ctl.assign(kCpuCtlSize, 0);
    m_prot_ram.assign(kProtSize, 0);
    m_luma.assign(0x8000, 0);
    m_textureram0.assign(kTextureSize, 0);
    m_textureram1.assign(kTextureSize, 0);

    // 0229 protection reads encrypted words from the shared source RAM.
    m_protection.reset();
    m_protection.set_source([this](std::uint32_t word_index) {
        const std::size_t off = (word_index & 0x3fff) * 2;
        if (off + 1 >= m_prot_ram.size())
            return static_cast<std::uint16_t>(0);
        return static_cast<std::uint16_t>(m_prot_ram[off] | (m_prot_ram[off + 1] << 8));
    });

    m_io.reset();
    m_io.set_in_port([this](int port) -> std::uint8_t {
        switch (port) {
            case 1: return in0_read();
            case 2: return m_inputs.p1;
            case 3: return m_inputs.p2;
            case 6: return m_inputs.dipsw;
            default: return 0xff;
        }
    });
    m_io.set_out_port([this](int port, std::uint8_t data) {
        if (port == 0)
            eeprom_write(data);
    });

    m_eeprom.reset();
    m_video.reset();
}

std::uint8_t* Bus::map(std::uint32_t addr, std::size_t len, bool& writable) const {
    writable = false;

    if (in_range(addr, len, kRomBase, m_rom_size))
        return const_cast<std::uint8_t*>(m_rom) + addr;

    if (in_range(addr, len, kRam0020Base, m_ram_0020.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_ram_0020.data()) + (addr - kRam0020Base);
    }
    if (in_range(addr, len, kWorkRamBase, m_workram.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_workram.data()) + (addr - kWorkRamBase);
    }
    if (in_range(addr, len, kGeoBase, m_geo.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_geo.data()) + (addr - kGeoBase);
    }
    if (in_range(addr, len, kGeoPrgBase, m_geo_prg.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_geo_prg.data()) + (addr - kGeoPrgBase);
    }
    if (in_mirror(addr, len, kBufferRamBase, m_bufferram.size(), 0x80000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_bufferram.data()) +
               ((addr - kBufferRamBase) % m_bufferram.size());
    }
    if (in_range(addr, len, kCpuCtlBase, m_cpu_ctl.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_cpu_ctl.data()) + (addr - kCpuCtlBase);
    }
    if (in_range(addr, len, kProtBase, m_prot_ram.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_prot_ram.data()) + (addr - kProtBase);
    }
    if (in_range(addr, len, kBackupBase, m_backup.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_backup.data()) + (addr - kBackupBase);
    }
    if (in_mirror(addr, len, kTexture0Base, m_textureram0.size(), 0x100000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_textureram0.data()) +
               ((addr - kTexture0Base) % m_textureram0.size());
    }
    if (in_mirror(addr, len, kTexture1Base, m_textureram1.size(), 0x100000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_textureram1.data()) +
               ((addr - kTexture1Base) % m_textureram1.size());
    }

    if (in_range(addr, len, kLumaBase, m_luma.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_luma.data()) + (addr - kLumaBase);
    }

    if (std::uint8_t* p = m_video.map(addr, len)) {
        writable = true;  // all video apertures are RAM
        return p;
    }

    if (m_roms) {
        if (const Region* md = m_roms->region("main_data")) {
            if (in_range(addr, len, kMainDataBase, md->data.size()))
                return const_cast<std::uint8_t*>(md->data.data()) + (addr - kMainDataBase);
            if (in_range(addr, len, kMainDataBase2, md->data.size()))
                return const_cast<std::uint8_t*>(md->data.data()) + (addr - kMainDataBase2);
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Register windows
// ---------------------------------------------------------------------------

bool Bus::read_register(std::uint32_t addr, unsigned lane, std::uint8_t& out) const {
    // 315-5649 I/O controller: registers 0..7, byte lanes 0 and 2 only.
    if (addr >= kIoBase && addr < kIoBase + 0x20) {
        if (lane != 0 && lane != 2) {
            out = 0xff;
            return true;
        }
        out = m_io.read((addr - kIoBase) >> 2);
        return true;
    }

    // 0x00980000: SHARC coprocessor and GEO control/status.
    if (addr >= kCoproCtlBase && addr < kCoproCtlBase + 0x1000) {
        switch ((addr - kCoproCtlBase) >> 2) {
            case 0:  // copro control 1
                out = byte_lane(m_coproctl, lane);
                return true;
            case 1:  // copro FIFO control: output FIFO always empty
                out = lane == 0 ? 1 : 0;
                return true;
            case 2:  // GEO control 1
                out = byte_lane(m_geoctl, lane);
                return true;
            case 3: {  // video control: frame parity used for scanout sync
                const std::uint32_t framenum = render_mode() ? ((m_framenum & 1) << 2)
                                                             : ((m_framenum & 2) << 1);
                out = byte_lane(framenum | (m_videoctl & 3), lane);
                return true;
            }
            case 5:  // copro status: 0xffffffff once a program has been uploaded
                out = byte_lane(m_coprocnt == 0 ? 0xffffffffu : 0u, lane);
                return true;
            default:
                out = 0xff;
                return true;
        }
    }

    // 0x009c0000 i8251 UART, byte lane 0. Stubbed: transmitter ready.
    if (addr >= kUartBase && addr < kUartBase + 8) {
        out = (lane == 0) ? 0x05 : 0x00;  // TxRDY | TxEMPTY
        return true;
    }

    // IRQ controller: request / enable.
    if (addr >= kIrqBase && addr < kIrqBase + 8) {
        out = byte_lane((addr - kIrqBase < 4) ? m_intreq : m_intena, lane);
        return true;
    }

    // 25 MHz countdown timers.
    if (addr >= kTimersBase && addr < kTimersBase + 0x10) {
        out = byte_lane(m_timer_val[(addr - kTimersBase) >> 2], lane);
        return true;
    }

    // Renderer mode register.
    if (addr >= kRenderModeBase && addr < kRenderModeBase + 0x200000) {
        out = byte_lane(m_render_mode, lane);
        return true;
    }
    if (addr >= kPolygonCountBase && addr < kPolygonCountBase + 0x200000) {
        out = 0;
        return true;
    }

    return false;
}

bool Bus::write_register(std::uint32_t addr, unsigned lane, std::uint8_t data) {
    if (addr >= kIoBase && addr < kIoBase + 0x20) {
        if (lane == 0 || lane == 2)
            m_io.write((addr - kIoBase) >> 2, data);
        return true;
    }

    if (addr >= kCoproCtlBase && addr < kCoproCtlBase + 0x1000) {
        const std::uint32_t mask = 0xffu << (lane * 8);
        switch ((addr - kCoproCtlBase) >> 2) {
            case 0: {  // copro control 1: high-bit edge starts/stops the upload
                const std::uint32_t nv =
                    (m_coproctl & ~mask) | (static_cast<std::uint32_t>(data) << (lane * 8));
                if ((nv ^ m_coproctl) == 0x80000000u && (nv & 0x80000000u))
                    m_coprocnt = 0;
                m_coproctl = nv;
                return true;
            }
            case 2:  // GEO control 1
                m_geoctl = (m_geoctl & ~mask) | (static_cast<std::uint32_t>(data) << (lane * 8));
                return true;
            case 3:  // video control
                m_videoctl = (m_videoctl & ~mask) | (static_cast<std::uint32_t>(data) << (lane * 8));
                return true;
            default:  // bank control register and others are write-only
                return true;
        }
    }

    if (addr >= kCoproFunctionBase && addr < kCoproFunctionBase + 0x4000)
        return true;  // SHARC function port; the coprocessor is stubbed
    if (addr >= kCoproFifoBase && addr < kCoproFifoBase + 0x4000)
        return true;
    if (addr >= kCoproIopBase && addr < kCoproIopBase + 0x1000)
        return true;

    if (addr >= kUartBase && addr < kUartBase + 8)
        return true;

    if (addr >= kIrqBase && addr < kIrqBase + 8) {
        const std::uint32_t mask = 0xffu << (lane * 8);
        if (addr - kIrqBase < 4) {
            // Write-one-to-clear acknowledge: the bits written as 1 are cleared
            // in the request register (MAME `m_intreq &= data`).
            const std::uint32_t ack = (static_cast<std::uint32_t>(data) << (lane * 8)) |
                                      ~mask;
            m_intreq &= ack;
            irq_update();
        } else {
            // Enable updates are delayed by two cycles; vcop2 relies on this.
            m_intena_pending = (m_intena_pending & ~mask) |
                               (static_cast<std::uint32_t>(data) << (lane * 8));
            m_intena_delay = 2;
            if (m_trace_irq)
                std::fprintf(stderr, "[irq] enable write addr=%08X data=%02X pending=%08X\n", addr,
                             data, m_intena_pending);
        }
        return true;
    }

    if (addr >= kTimersBase && addr < kTimersBase + 0x10) {
        const unsigned t = (addr - kTimersBase) >> 2;
        const std::uint32_t mask = 0xffu << (lane * 8);
        m_timer_val[t] = (m_timer_val[t] & ~mask) | (static_cast<std::uint32_t>(data) << (lane * 8));
        m_timer_orig[t] = m_timer_val[t];
        m_timer_run[t] = true;
        return true;
    }

    // 317-0229 protection source index and table upload port.
    if (addr >= kProtBase + 0x7ff0 && addr < kProtBase + 0x7ff4) {
        m_prot_source = (m_prot_source & ~(0xffu << (lane * 8))) |
                        (static_cast<std::uint32_t>(data) << (lane * 8));
        m_protection.source_w(m_prot_source);
        m_doa_dummy = true;
        return true;
    }
    if (addr >= kProtBase + 0x7ff4 && addr < kProtBase + 0x7ff8) {
        const unsigned offset = (addr - (kProtBase + 0x7ff4)) >> 1;
        m_protection.table_w(offset, static_cast<std::uint16_t>(data) << ((lane & 1) * 8));
        return true;
    }

    if (addr >= kRenderModeBase && addr < kRenderModeBase + 0x200000) {
        const std::uint32_t mask = 0xffu << (lane * 8);
        m_render_mode = (m_render_mode & ~mask) | (static_cast<std::uint32_t>(data) << (lane * 8));
        m_video.set_render_test_mode((m_render_mode & 1) != 0);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Byte / word / dword access
// ---------------------------------------------------------------------------

void Bus::watch_log(char op, std::uint32_t addr, std::uint32_t data) const {
    if (m_watch_limit == 0 || addr < m_watch_lo || addr > m_watch_hi)
        return;
    if (op == 'R' && !m_watch_reads)
        return;
    if (m_watch_count++ >= m_watch_limit)
        return;
    const std::uint32_t pc = m_cpu ? m_cpu->get_ip() : 0;
    std::fprintf(stderr, "[watch] %c pc=%08X addr=%08X data=%08X\n", op, pc, addr, data);
}

std::uint8_t Bus::raw_read8(std::uint32_t addr) const {
    bool w = false;
    if (const std::uint8_t* p = map(addr, 1, w))
        return *p;

    std::uint8_t out = 0xff;
    if (read_register(addr, addr & 3, out))
        return out;

    ++unmapped_reads;
    if (m_trace) ++m_unmapped_read_hist[addr & 0xfffc0000];
    if (m_trace && m_unmapped_read_log.size() < 40)
        m_unmapped_read_log.push_back({addr, m_cpu ? m_cpu->get_ip() : 0});
    return 0;
}

std::uint8_t Bus::read8(std::uint32_t addr) const {
    if (m_trace) ++m_read_hist[addr & 0xfffc0000];
    const std::uint8_t v = raw_read8(addr);
    watch_log('R', addr, v);
    return v;
}

std::uint16_t Bus::read16(std::uint32_t addr) const {
    watch_log('R', addr, 0);
    bool w = false;
    if (const std::uint8_t* p = map(addr, 2, w))
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));

    // 317-0229 data port: two decompressed bytes, high byte first. The first
    // halfword after a source write is discarded by Dead or Alive.
    if (addr >= kProtBase + 0x7ff8 && addr < kProtBase + 0x7ffc) {
        if (m_doa_dummy) {
            m_doa_dummy = false;
            return 0;
        }
        return m_protection.data_r();
    }
    // Dead or Alive status location: alternates per access.
    if (addr >= 0x01d8400c && addr < 0x01d84010) {
        m_prot_a = !m_prot_a;
        return m_prot_a ? 0xffff : 0xfff0;
    }

    return static_cast<std::uint16_t>(read8(addr) | (read8(addr + 1) << 8));
}

std::uint32_t Bus::read32(std::uint32_t addr) const {
    watch_log('R', addr, 0);
    if (m_trace) ++m_read_hist[addr & 0xfffc0000];
    bool w = false;
    if (const std::uint8_t* p = map(addr, 4, w))
        return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                          (static_cast<std::uint32_t>(p[3]) << 24));

    if (addr >= kProtBase + 0x7ff8 && addr < kProtBase + 0x7ffc) {
        const std::uint32_t lo = read16(addr);
        const std::uint32_t hi = read16(addr + 2);
        return lo | (hi << 16);
    }

    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(raw_read8(addr + i)) << (i * 8);
    return v;
}

void Bus::write8(std::uint32_t addr, std::uint8_t val) {
    if (m_trace) ++m_write_hist[addr & 0xfffc0000];
    watch_log('W', addr, val);
    bool w = false;
    if (std::uint8_t* p = map(addr, 1, w)) {
        if (w) {
            *p = val;
            if (addr >= 0x01800000 && addr < 0x01804000)
                m_video.mark_palette_dirty();
            return;
        }
        ++rom_writes;
        ++m_rom_write_pages[addr & 0xfffc0000];
        return;
    }
    if (write_register(addr, addr & 3, val))
        return;
    if (is_nopw(addr))
        return;
    ++unmapped_writes;
    ++m_unmapped_write_hist[addr & 0xfffc0000];
}

void Bus::write16(std::uint32_t addr, std::uint16_t val) {
    watch_log('W', addr, val);
    bool w = false;
    if (std::uint8_t* p = map(addr, 2, w)) {
        if (w) {
            p[0] = static_cast<std::uint8_t>(val);
            p[1] = static_cast<std::uint8_t>(val >> 8);
            if (addr >= 0x01800000 && addr < 0x01804000)
                m_video.mark_palette_dirty();
            return;
        }
        ++rom_writes;
        return;
    }

    if (addr >= kProtBase + 0x7ff4 && addr < kProtBase + 0x7ff8) {
        m_protection.table_w((addr - (kProtBase + 0x7ff4)) >> 1, val);
        return;
    }

    write8(addr, static_cast<std::uint8_t>(val));
    write8(addr + 1, static_cast<std::uint8_t>(val >> 8));
}

void Bus::write32(std::uint32_t addr, std::uint32_t val) {
    watch_log('W', addr, val);
    bool w = false;
    if (std::uint8_t* p = map(addr, 4, w)) {
        if (w) {
            p[0] = static_cast<std::uint8_t>(val);
            p[1] = static_cast<std::uint8_t>(val >> 8);
            p[2] = static_cast<std::uint8_t>(val >> 16);
            p[3] = static_cast<std::uint8_t>(val >> 24);
            if (addr >= 0x01800000 && addr < 0x01804000)
                m_video.mark_palette_dirty();
            return;
        }
        ++rom_writes;
        return;
    }

    if (addr >= kGeoBase && addr < kGeoBase + 0x4000) {
        geo_write(addr - kGeoBase, val);
        return;
    }

    if (is_nopw(addr))
        return;

    if (addr >= kCoproFunctionBase && addr < kCoproFunctionBase + 0x4000) {
        copro_function_port_w(addr - kCoproFunctionBase, val);
        return;
    }
    if (addr >= kCoproFifoBase && addr < kCoproFifoBase + 0x4000) {
        copro_fifo_w(val);
        return;
    }
    if (addr >= kCoproIopBase && addr < kCoproIopBase + 0x1000) {
        copro_fifo_w(val);
        return;
    }

    if (addr >= kProtBase + 0x7ff0 && addr < kProtBase + 0x7ff4) {
        m_prot_source = val;
        m_protection.source_w(val);
        m_doa_dummy = true;
        return;
    }
    if (addr >= kProtBase + 0x7ff4 && addr < kProtBase + 0x7ff8) {
        m_protection.table_w(0, static_cast<std::uint16_t>(val));
        m_protection.table_w(1, static_cast<std::uint16_t>(val >> 16));
        return;
    }

    for (unsigned i = 0; i < 4; ++i)
        write8(addr + i, static_cast<std::uint8_t>(val >> (i * 8)));
}

bool Bus::burst(std::uint32_t addr) const {
    bool w = false;
    return map(addr, 4, w) != nullptr && w;
}

std::string Bus::describe(std::uint32_t addr) const {
    bool w = false;
    if (map(addr, 1, w))
        return w ? "RAM" : "ROM";
    std::uint8_t dummy = 0;
    return read_register(addr, addr & 3, dummy) ? "register" : "unmapped";
}

// ---------------------------------------------------------------------------
// Peripherals
// ---------------------------------------------------------------------------

void Bus::eeprom_write(std::uint8_t data) {
    m_ctrlmode = data & 1;
    m_eeprom.di_write((data >> 5) & 1);
    m_eeprom.clk_write((data >> 7) & 1);
    m_eeprom.cs_write((data >> 6) & 1);
}

std::uint8_t Bus::in0_read() const {
    std::uint8_t data = 0xff;
    if (m_inputs.coin1) data &= ~0x01;
    if (m_inputs.coin2) data &= ~0x02;
    if (m_inputs.test) data &= ~0x04;
    if (m_inputs.service) data &= ~0x08;
    if (m_inputs.start1) data &= ~0x10;
    if (m_inputs.start2) data &= ~0x20;

    if (m_ctrlmode)
        return static_cast<std::uint8_t>(0xc0 | (m_eeprom.do_read() << 5) | 0x10 | (data & 0x0f));
    return data;
}

void Bus::irq_update() {
    if (!m_cpu)
        return;
    m_cpu->set_input(0, (m_intreq & 0x001) ? 1 : 0);
    m_cpu->set_input(1, (m_intreq & 0x002) ? 1 : 0);
    m_cpu->set_input(2, (m_intreq & 0x3fc) ? 1 : 0);
    m_cpu->set_input(3, (m_intreq & 0xc00) ? 1 : 0);
}

void Bus::tick(int cycles) {
    if (m_intena_delay > 0) {
        m_intena_delay -= cycles;
        if (m_intena_delay <= 0) {
            m_intena_delay = 0;
            m_intena = m_intena_pending;
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (!m_timer_run[i])
            continue;
        const std::uint32_t c = static_cast<std::uint32_t>(cycles);
        if (m_timer_val[i] > c) {
            m_timer_val[i] -= c;
            continue;
        }
        m_timer_val[i] = 0xfffff;
        m_timer_run[i] = false;
        ++m_timer_irq_count;
        const std::uint32_t line = 1u << (i + 2);
        if (m_intena & line) {
            m_intreq |= line;
            irq_update();
        }
    }
}

void Bus::trigger_vblank() {
    m_framenum++;
    ++m_vblank_count;

    const std::uint32_t line = 1u << 0;
    if (m_intena & line) {
        m_intreq |= line;
        irq_update();
    }
}

void Bus::geo_write(std::uint32_t offset, std::uint32_t data) {
    const std::uint32_t address = offset;
    if (address < 0x1000) {
        std::uint32_t r;
        if (data & 0x80000000) {
            r = (data & 0x800fffff) | (((address >> 4) & 0x3f) << 23);
        } else if ((address & 0xf) == 0) {
            r = (data & 0x000fffff) | (((address >> 4) & 0x3f) << 23);
            if ((address >> 4) & 0xc0) {
                if (((address >> 4) & 0x3f) == 1)
                    r |= ((address >> 10) & 3) << 29;  // eye mode (Sega Rally)
            }
        } else {
            return;
        }
        m_bufferram[(m_geo_write_addr / 4) % (m_bufferram.size() / 4)] = r;
        m_geo_write_addr += 4;
    } else if (address == 0x1008) {
        m_geo_write_addr = data & 0xfffff;
    } else if (address == 0x3008) {
        m_geo_read_addr = data & 0xfffff;
    }
}

std::uint32_t Bus::geo_read(std::uint32_t offset) const {
    if (offset == 0x2008)
        return m_geo_write_addr;
    if (offset == 0x3008)
        return m_geo_read_addr;
    return 0;
}

void Bus::copro_fifo_w(std::uint32_t data) {
    (void)data;
    if (m_coproctl & 0x80000000)
        m_coprocnt++;  // SHARC program upload; the coprocessor is stubbed
}

void Bus::copro_function_port_w(std::uint32_t offset, std::uint32_t data) {
    const std::uint32_t d = (data & 0x800fffff) | (((offset >> 2) & 0xff) << 23);
    (void)d;  // pushed into the SHARC input FIFO; stubbed.
}

}  // namespace sage
