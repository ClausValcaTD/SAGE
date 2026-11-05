#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <string>
#include <vector>

#include "sage/cpu/i960.h"
#include "sage/cpu/sharc.h"
#include "sage/eeprom.h"
#include "sage/geometry.h"
#include "sage/io.h"
#include "sage/protection.h"
#include "sage/rom_loader.h"
#include "sage/video.h"

namespace sage {

// Sega Model 2B memory interconnect and peripheral set.
//
// Implements MAME's model2b_0229_mem() map for Dead or Alive: program ROM,
// work RAM, the GEO/SHARC buffer RAM, the interrupt and timer controllers, the
// 315-5649 I/O controller (inputs, DIP switches, EEPROM), the 317-0229
// protection device, and the video apertures. Unmapped accesses are counted and
// returned as 0 rather than faulting.
class Bus : public cpu::BusInterface {
public:
    // Active-low cabinet inputs and DIP switches.
    struct InputState {
        bool coin1 = false;
        bool coin2 = false;
        bool test = false;  // service mode (SW2)
        bool service = false;
        bool start1 = false;
        bool start2 = false;
        std::uint8_t p1 = 0xff;  // buttons, active low
        std::uint8_t p2 = 0xff;
        std::uint8_t dipsw = 0xff;
    };

    Bus() = default;

    void attach(const RomLoader& roms);
    void set_cpu(cpu::I960* cpu) { m_cpu = cpu; }
    void set_inputs(const InputState& in) { m_inputs = in; }
    InputState& inputs() { return m_inputs; }

    std::uint8_t read8(std::uint32_t addr) const override;
    std::uint16_t read16(std::uint32_t addr) const override;
    std::uint32_t read32(std::uint32_t addr) const override;
    void write8(std::uint32_t addr, std::uint8_t val) override;
    void write16(std::uint32_t addr, std::uint16_t val) override;
    void write32(std::uint32_t addr, std::uint32_t val) override;

    bool burst(std::uint32_t addr) const override;

    // ---- Timing and interrupts -------------------------------------------
    // Advances the four 25 MHz countdown timers by `cycles`.
    void tick(int cycles);
    // VBLANK: latches the frame parity and raises IRQ0 when enabled.
    void trigger_vblank();
    std::uint32_t frame_number() const { return m_framenum; }
    std::uint64_t vblank_count() const { return m_vblank_count; }
    std::uint64_t timer_irq_count() const { return m_timer_irq_count; }
    std::uint32_t irq_enable() const { return m_intena; }
    std::uint32_t irq_request() const { return m_intreq; }
    std::uint32_t video_control() const { return m_videoctl; }

    // ---- SHARC coprocessor ------------------------------------------------
    // The ADSP-21062 runs the motion/collision program the i960 uploads through
    // the coprocessor FIFOs. tick_copro() advances it in step with the i960.
    void tick_copro(int cycles);
    bool copro_halted() const { return m_copro_halted; }
    std::uint64_t copro_instructions() const { return m_sharc.instructions(); }
    std::uint32_t copro_pc() const { return m_sharc.pc(); }
    const cpu::Sharc& sharc() const { return m_sharc; }
    cpu::Sharc& sharc_mutable() { return m_sharc; }
    const std::string& copro_fault() const { return m_copro_fault; }

    // ---- Video ------------------------------------------------------------
    // Render mode bit 2: true = 60 Hz, false = 30 Hz (matches MAME).
    bool render_mode() const { return (m_render_mode >> 2) & 1; }

    Video& video() { return m_video; }
    const Video& video() const { return m_video; }
    const std::uint32_t* framebuffer() const { return m_video.rgba(); }

    // ---- 3D geometry ------------------------------------------------------
    Geometry& geometry() { return m_geometry; }
    const Geometry& geometry() const { return m_geometry; }

    // Diagnostic counters.
    mutable std::uint64_t unmapped_reads = 0;
    mutable std::uint64_t unmapped_writes = 0;
    mutable std::uint64_t rom_writes = 0;

    // Bring-up aid: per-64 KiB-page access histogram when enabled.
    void set_trace(bool on) { m_trace = on; }
    void set_trace_irq(bool on) { m_trace_irq = on; }
    bool trace() const { return m_trace; }
    const std::map<std::uint32_t, std::uint64_t>& read_histogram() const { return m_read_hist; }
    const std::map<std::uint32_t, std::uint64_t>& write_histogram() const { return m_write_hist; }
    const std::map<std::uint32_t, std::uint64_t>& unmapped_read_histogram() const {
        return m_unmapped_read_hist;
    }
    const std::map<std::uint32_t, std::uint64_t>& unmapped_write_histogram() const {
        return m_unmapped_write_hist;
    }
    const std::map<std::uint32_t, std::uint64_t>& rom_write_histogram() const {
        return m_rom_write_pages;
    }

    std::string describe(std::uint32_t addr) const;
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& unmapped_read_log() const {
        return m_unmapped_read_log;
    }

    // Bring-up aid: log the first accesses inside [lo, hi] with the PC.
    void set_watch(std::uint32_t lo, std::uint32_t hi, std::size_t limit = 64, bool reads = false) {
        m_watch_lo = lo;
        m_watch_hi = hi;
        m_watch_limit = limit;
        m_watch_reads = reads;
    }

private:
    // Register-window byte access; returns true when `addr` belongs to a
    // peripheral register block.
    bool read_register(std::uint32_t addr, unsigned lane, std::uint8_t& out) const;
    bool write_register(std::uint32_t addr, unsigned lane, std::uint8_t data);

    void irq_update();
    void eeprom_write(std::uint8_t data);
    std::uint8_t in0_read() const;
    void copro_fifo_w(std::uint32_t data);
    void copro_function_port_w(std::uint32_t offset, std::uint32_t data);
    std::uint32_t copro_fifo_r() const;
    void copro_ctl1_write(std::uint32_t value);
    void copro_update_flags() const;
    void copro_boot();
    void copro_halt();

    // Board-side coprocessor FIFOs (MAME generic_fifo_u32_device).
    struct CoproFifo {
        std::size_t cap = 16;
        std::deque<std::uint32_t> q;
        bool empty() const { return q.empty(); }
        bool full() const { return q.size() >= cap; }
        void clear() { q.clear(); }
        void push(std::uint32_t v) { q.push_back(v); }
        std::uint32_t pop() {
            if (q.empty()) return 0;
            const std::uint32_t v = q.front();
            q.pop_front();
            return v;
        }
    };
    mutable CoproFifo m_fifo_in;
    mutable CoproFifo m_fifo_out;

    std::uint8_t* map(std::uint32_t addr, std::size_t len, bool& writable) const;
    std::uint8_t raw_read8(std::uint32_t addr) const;

    const std::uint8_t* m_rom = nullptr;
    std::size_t m_rom_size = 0;

    std::vector<std::uint8_t> m_workram;      // 0x00500000 (1 MiB)
    std::vector<std::uint8_t> m_ram_0020;     // 0x00200000 (256 KiB)
    std::vector<std::uint8_t> m_backup;       // 0x01d00000 (16 KiB)
    std::vector<std::uint8_t> m_bufferram;    // 0x00900000 (128 KiB, 0x80000-mirrored)
    std::vector<std::uint8_t> m_cpu_ctl;      // 0x00e00000 (0x38)
    std::vector<std::uint8_t> m_prot_ram;     // 0x01d80000 (32 KiB, 0229 source window)
    std::vector<std::uint8_t> m_luma;         // 0x11400000 (32 KiB)
    std::vector<std::uint8_t> m_textureram0;  // 0x11000000 (1 MiB, 0x100000-mirrored)
    std::vector<std::uint8_t> m_textureram1;  // 0x11200000 (1 MiB, 0x100000-mirrored)

    Video m_video;
    Geometry m_geometry;
    mutable Io3155649 m_io;  // register reads have side effects
    Eeprom93C46 m_eeprom;
    mutable Sega3155838Comp m_protection{Sega3155838Comp::Variant::Sega3170229};
    mutable bool m_doa_dummy = true;
    mutable bool m_prot_a = false;
    std::uint32_t m_prot_source = 0;

    // Interrupt controller (0x00e80000).
    std::uint32_t m_intreq = 0;
    std::uint32_t m_intena = 0;
    std::uint32_t m_intena_pending = 0;
    int m_intena_delay = 0;

    // CPU control / video control.
    std::uint32_t m_videoctl = 0;
    std::uint32_t m_render_mode = 0;

    // Timers (0x00f00000), 25 MHz countdown.
    std::uint32_t m_timer_val[4] = {0xfffff, 0xfffff, 0xfffff, 0xfffff};
    std::uint32_t m_timer_orig[4] = {0xfffff, 0xfffff, 0xfffff, 0xfffff};
    bool m_timer_run[4] = {false, false, false, false};

    // GEO upload state is owned by the Geometry module (m_geometry).
    std::uint32_t m_geoctl = 0;

    // SHARC coprocessor mailbox and interpreter.
    mutable std::uint32_t m_coproctl = 0;
    mutable std::uint32_t m_coprocnt = 0;
    mutable cpu::Sharc m_sharc;
    std::vector<std::uint8_t> m_copro_data;
    mutable bool m_copro_halted = false;
    mutable bool m_copro_program_upload = false;
    mutable std::uint64_t m_copro_phase = 0;
    mutable std::string m_copro_fault;

    InputState m_inputs;
    std::uint32_t m_framenum = 0;
    std::uint64_t m_vblank_count = 0;
    mutable std::uint64_t m_sharc_ext_log = 0;
    std::uint64_t m_timer_irq_count = 0;
    std::uint8_t m_ctrlmode = 0;

    cpu::I960* m_cpu = nullptr;
    const RomLoader* m_roms = nullptr;

    bool m_trace = false;
    bool m_trace_irq = false;
    std::uint32_t m_watch_lo = 0;
    std::uint32_t m_watch_hi = 0;
    std::size_t m_watch_limit = 0;
    bool m_watch_reads = false;
    mutable std::size_t m_watch_count = 0;
    void watch_log(char op, std::uint32_t addr, std::uint32_t data) const;
    mutable std::map<std::uint32_t, std::uint64_t> m_read_hist;
    mutable std::map<std::uint32_t, std::uint64_t> m_write_hist;
    mutable std::map<std::uint32_t, std::uint64_t> m_unmapped_read_hist;
    mutable std::map<std::uint32_t, std::uint64_t> m_unmapped_write_hist;
    mutable std::map<std::uint32_t, std::uint64_t> m_rom_write_pages;
    mutable std::vector<std::pair<std::uint32_t, std::uint32_t>> m_unmapped_read_log;
};

}  // namespace sage
