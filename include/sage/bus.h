#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sage/cpu/i960.h"
#include "sage/rom_loader.h"

namespace sage {

// Sega Model 2B memory interconnect.
//
// Maps the loaded ROM regions and the Model 2 work RAM into the i960's 32-bit
// address space, mirroring the subset of MAME's model2b_0229_mem() map that the
// boot path actually touches. Unmapped accesses are counted and returned as 0
// rather than faulting, so a partially-implemented system still steps safely.
class Bus : public cpu::BusInterface {
public:
    Bus() = default;

    // Installs the loaded regions: "maincpu" becomes program ROM at 0x00000000,
    // "main_data" is mapped read-only at 0x02000000 (and 0x06000000 for the
    // 0x1000000 offset half). Allocates the work RAM buffers.
    void attach(const RomLoader& roms);

    std::uint8_t read8(std::uint32_t addr) const override;
    std::uint16_t read16(std::uint32_t addr) const override;
    std::uint32_t read32(std::uint32_t addr) const override;
    void write8(std::uint32_t addr, std::uint8_t val) override;
    void write16(std::uint32_t addr, std::uint16_t val) override;
    void write32(std::uint32_t addr, std::uint32_t val) override;

    bool burst(std::uint32_t addr) const override;

    // Diagnostic counters.
    mutable std::uint64_t unmapped_reads = 0;
    mutable std::uint64_t unmapped_writes = 0;
    mutable std::uint64_t rom_writes = 0;

    // Human-readable name of the device/memory backing `addr` (for trace logs).
    std::string describe(std::uint32_t addr) const;

private:
    // Resolves a 32-bit address to a host buffer, or nullptr if unmapped.
    // `writable` is set when the target is RAM (writes are dropped otherwise).
    std::uint8_t* map(std::uint32_t addr, std::size_t len, bool& writable) const;
    void store(std::uint32_t addr, const std::uint8_t* src, std::size_t len);
    void fetch(std::uint32_t addr, std::uint8_t* dst, std::size_t len) const;

    const std::uint8_t* m_rom = nullptr;       // maincpu program ROM
    std::size_t m_rom_size = 0;

    std::vector<std::uint8_t> m_workram;       // 0x00500000 (1 MiB)
    std::vector<std::uint8_t> m_ram_0020;      // 0x00200000 (256 KiB)
    std::vector<std::uint8_t> m_backup;        // 0x01d00000 (16 KiB)
    std::vector<std::uint8_t> m_bufferram;     // 0x00900000 (128 KiB, 0x80000-mirrored)
    std::vector<std::uint8_t> m_geo;           // 0x00800000 (16 KiB)
    std::vector<std::uint8_t> m_cpu_ctl;       // 0x00e00000 (0x38, CPU wait-states)
    std::vector<std::uint8_t> m_irq_ctl;       // 0x00e80000 (8)
    std::vector<std::uint8_t> m_timers;        // 0x00f00000 (16)
    std::vector<std::uint8_t> m_palette;       // 0x01800000 (16 KiB)
    std::vector<std::uint8_t> m_textureram0;   // 0x11000000 (1 MiB, 0x100000-mirrored)
    std::vector<std::uint8_t> m_textureram1;   // 0x11200000 (1 MiB, 0x100000-mirrored)
    std::vector<std::uint8_t> m_lumaram;       // 0x11400000 (64 KiB)

    const RomLoader* m_roms = nullptr;
};

}  // namespace sage
