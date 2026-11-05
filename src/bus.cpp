#include "sage/bus.h"

#include <cstring>

namespace sage {
namespace {

constexpr std::uint32_t kRomBase = 0x00000000;  // maincpu program ROM
constexpr std::uint32_t kRam0020Base = 0x00200000;
constexpr std::uint32_t kWorkRamBase = 0x00500000;
constexpr std::uint32_t kGeoBase = 0x00800000;
constexpr std::uint32_t kBufferRamBase = 0x00900000;
constexpr std::uint32_t kCpuCtlBase = 0x00e00000;
constexpr std::uint32_t kIrqCtlBase = 0x00e80000;
constexpr std::uint32_t kTimersBase = 0x00f00000;
constexpr std::uint32_t kPaletteBase = 0x01800000;
constexpr std::uint32_t kBackupBase = 0x01d00000;
constexpr std::uint32_t kMainDataBase = 0x02000000;
constexpr std::uint32_t kMainDataBase2 = 0x06000000;
constexpr std::uint32_t kTextureRam0Base = 0x11000000;
constexpr std::uint32_t kTextureRam1Base = 0x11200000;
constexpr std::uint32_t kLumaRamBase = 0x11400000;

constexpr std::size_t kWorkRamSize = 0x00100000;
constexpr std::size_t kRam0020Size = 0x00040000;
constexpr std::size_t kBackupSize = 0x00004000;
constexpr std::size_t kBufferRamSize = 0x00020000;   // mirrored across 0x80000
constexpr std::size_t kGeoSize = 0x00004000;
constexpr std::size_t kCpuCtlSize = 0x00000038;
constexpr std::size_t kIrqCtlSize = 0x00000008;
constexpr std::size_t kTimersSize = 0x00000010;
constexpr std::size_t kPaletteSize = 0x00004000;
constexpr std::size_t kTextureRamSize = 0x00100000;  // mirrored across 0x100000
constexpr std::size_t kLumaRamSize = 0x00010000;

// True when [addr, addr+len) lies inside [base, base+size).
bool in_range(std::uint32_t addr, std::size_t len, std::uint32_t base, std::size_t size) {
    return addr >= base && static_cast<std::uint64_t>(addr) + len <= static_cast<std::uint64_t>(base) + size;
}

// True when [addr, addr+len) lies inside `size` bytes starting at `base`,
// repeated every `span` bytes (MAME's .mirror()).
bool in_mirror(std::uint32_t addr, std::size_t len, std::uint32_t base, std::size_t size,
               std::size_t span) {
    if (addr < base)
        return false;
    const std::uint64_t off = addr - base;
    if (off % span + len > size)
        return false;
    return static_cast<std::uint64_t>(addr) + len <= static_cast<std::uint64_t>(base) + span;
}

}  // namespace

void Bus::attach(const RomLoader& roms) {
    m_roms = &roms;

    const Region* cpu = roms.region("maincpu");
    if (cpu) {
        m_rom = cpu->data.data();
        m_rom_size = cpu->data.size();
    }

    m_workram.assign(kWorkRamSize, 0);
    m_ram_0020.assign(kRam0020Size, 0);
    m_backup.assign(kBackupSize, 0);
    m_bufferram.assign(kBufferRamSize, 0);
    m_geo.assign(kGeoSize, 0);
    m_cpu_ctl.assign(kCpuCtlSize, 0);
    m_irq_ctl.assign(kIrqCtlSize, 0);
    m_timers.assign(kTimersSize, 0);
    m_palette.assign(kPaletteSize, 0);
    m_textureram0.assign(kTextureRamSize, 0);
    m_textureram1.assign(kTextureRamSize, 0);
    m_lumaram.assign(kLumaRamSize, 0);
}

std::uint8_t* Bus::map(std::uint32_t addr, std::size_t len, bool& writable) const {
    writable = false;

    if (in_range(addr, len, kRomBase, m_rom_size))
        return const_cast<std::uint8_t*>(m_rom) + (addr - kRomBase);

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
    if (in_mirror(addr, len, kBufferRamBase, m_bufferram.size(), 0x80000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_bufferram.data()) +
               ((addr - kBufferRamBase) % m_bufferram.size());
    }
    if (in_range(addr, len, kCpuCtlBase, m_cpu_ctl.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_cpu_ctl.data()) + (addr - kCpuCtlBase);
    }
    if (in_range(addr, len, kIrqCtlBase, m_irq_ctl.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_irq_ctl.data()) + (addr - kIrqCtlBase);
    }
    if (in_range(addr, len, kTimersBase, m_timers.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_timers.data()) + (addr - kTimersBase);
    }
    if (in_range(addr, len, kPaletteBase, m_palette.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_palette.data()) + (addr - kPaletteBase);
    }
    if (in_range(addr, len, kBackupBase, m_backup.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_backup.data()) + (addr - kBackupBase);
    }
    if (in_mirror(addr, len, kTextureRam0Base, m_textureram0.size(), 0x100000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_textureram0.data()) +
               ((addr - kTextureRam0Base) % m_textureram0.size());
    }
    if (in_mirror(addr, len, kTextureRam1Base, m_textureram1.size(), 0x100000)) {
        writable = true;
        return const_cast<std::uint8_t*>(m_textureram1.data()) +
               ((addr - kTextureRam1Base) % m_textureram1.size());
    }
    if (in_range(addr, len, kLumaRamBase, m_lumaram.size())) {
        writable = true;
        return const_cast<std::uint8_t*>(m_lumaram.data()) + (addr - kLumaRamBase);
    }

    // main_data is read-only ROM, mapped at two base addresses.
    if (m_roms) {
        const Region* md = m_roms->region("main_data");
        if (md) {
            if (in_range(addr, len, kMainDataBase, md->data.size()))
                return const_cast<std::uint8_t*>(md->data.data()) + (addr - kMainDataBase);
            if (in_range(addr, len, kMainDataBase2, md->data.size()))
                return const_cast<std::uint8_t*>(md->data.data()) + (addr - kMainDataBase2);
        }
    }

    return nullptr;
}

std::uint8_t Bus::read8(std::uint32_t addr) const {
    bool w = false;
    if (const std::uint8_t* p = map(addr, 1, w))
        return *p;
    ++unmapped_reads;
    return 0;
}

std::uint16_t Bus::read16(std::uint32_t addr) const {
    bool w = false;
    if (const std::uint8_t* p = map(addr, 2, w))
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    ++unmapped_reads;
    return 0;
}

std::uint32_t Bus::read32(std::uint32_t addr) const {
    bool w = false;
    if (const std::uint8_t* p = map(addr, 4, w))
        return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                          (static_cast<std::uint32_t>(p[3]) << 24));
    ++unmapped_reads;
    return 0;
}

void Bus::store(std::uint32_t addr, const std::uint8_t* src, std::size_t len) {
    bool w = false;
    std::uint8_t* p = map(addr, len, w);
    if (p && w) {
        std::memcpy(p, src, len);
        return;
    }
    if (p)  // ROM: read-only
        ++rom_writes;
    else
        ++unmapped_writes;
}

void Bus::fetch(std::uint32_t addr, std::uint8_t* dst, std::size_t len) const {
    bool w = false;
    if (const std::uint8_t* p = map(addr, len, w)) {
        std::memcpy(dst, p, len);
        return;
    }
    std::memset(dst, 0, len);
    ++unmapped_reads;
}

void Bus::write8(std::uint32_t addr, std::uint8_t val) { store(addr, &val, 1); }
void Bus::write16(std::uint32_t addr, std::uint16_t val) {
    const std::uint8_t b[2] = {static_cast<std::uint8_t>(val & 0xff),
                               static_cast<std::uint8_t>(val >> 8)};
    store(addr, b, 2);
}
void Bus::write32(std::uint32_t addr, std::uint32_t val) {
    const std::uint8_t b[4] = {static_cast<std::uint8_t>(val & 0xff),
                               static_cast<std::uint8_t>((val >> 8) & 0xff),
                               static_cast<std::uint8_t>((val >> 16) & 0xff),
                               static_cast<std::uint8_t>((val >> 24) & 0xff)};
    store(addr, b, 4);
}

bool Bus::burst(std::uint32_t addr) const {
    // Plain ROM and RAM support burst; register/FIFO apertures do not.
    if (in_range(addr, 4, kRomBase, m_rom_size))
        return true;
    if (m_roms) {
        const Region* md = m_roms->region("main_data");
        if (md && (in_range(addr, 4, kMainDataBase, md->data.size()) ||
                   in_range(addr, 4, kMainDataBase2, md->data.size())))
            return true;
    }
    if (in_range(addr, 4, kWorkRamBase, m_workram.size()))
        return true;
    if (in_range(addr, 4, kRam0020Base, m_ram_0020.size()))
        return true;
    if (in_range(addr, 4, kBackupBase, m_backup.size()))
        return true;
    if (in_range(addr, 4, kPaletteBase, m_palette.size()))
        return true;
    if (in_mirror(addr, 4, kBufferRamBase, m_bufferram.size(), 0x80000))
        return true;
    if (in_mirror(addr, 4, kTextureRam0Base, m_textureram0.size(), 0x100000))
        return true;
    if (in_mirror(addr, 4, kTextureRam1Base, m_textureram1.size(), 0x100000))
        return true;
    if (in_range(addr, 4, kLumaRamBase, m_lumaram.size()))
        return true;
    return false;
}

std::string Bus::describe(std::uint32_t addr) const {
    if (in_range(addr, 1, kRomBase, m_rom_size))
        return "maincpu ROM";
    if (in_range(addr, 1, kRam0020Base, m_ram_0020.size()))
        return "work RAM (0x00200000)";
    if (in_range(addr, 1, kWorkRamBase, m_workram.size()))
        return "work RAM (0x00500000)";
    if (in_range(addr, 1, kGeoBase, m_geo.size()))
        return "geo registers";
    if (in_mirror(addr, 1, kBufferRamBase, m_bufferram.size(), 0x80000))
        return "buffer RAM";
    if (in_range(addr, 1, kCpuCtlBase, m_cpu_ctl.size()))
        return "CPU control";
    if (in_range(addr, 1, kIrqCtlBase, m_irq_ctl.size()))
        return "IRQ control";
    if (in_range(addr, 1, kTimersBase, m_timers.size()))
        return "timers";
    if (in_range(addr, 1, kPaletteBase, m_palette.size()))
        return "palette";
    if (in_range(addr, 1, kBackupBase, m_backup.size()))
        return "backup SRAM";
    if (in_mirror(addr, 1, kTextureRam0Base, m_textureram0.size(), 0x100000) ||
        in_mirror(addr, 1, kTextureRam1Base, m_textureram1.size(), 0x100000))
        return "texture RAM";
    if (in_range(addr, 1, kLumaRamBase, m_lumaram.size()))
        return "luma RAM";
    if (m_roms) {
        const Region* md = m_roms->region("main_data");
        if (md && (in_range(addr, 1, kMainDataBase, md->data.size()) ||
                   in_range(addr, 1, kMainDataBase2, md->data.size())))
            return "main_data ROM";
    }
    return "unmapped";
}

}  // namespace sage
