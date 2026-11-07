#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sage/cpu/m68000.h"
#include "sage/hw/compat.h"
#include "sage/hw/scsp.h"

namespace sage::hw {

// Model 2 sound board: a 68EC000 with 512 KB of RAM and a Yamaha YMF292-F (SCSP).
//
// The memory map follows MAME's model2_snd in src/mame/sega/model2.cpp region
// for region:
//
//   0x000000-0x07ffff  work RAM, shared with the SCSP
//   0x100000-0x100fff  SCSP registers
//   0x400000           sample banking (write only, inert for DOA)
//   0x600000-0x67ffff  sound program ROM ("audiocpu")
//   0x800000-0x9fffff  sample ROM ("samples")
//   0xa00000-0xdfffff  banked sample window 4
//   0xe00000-0xffffff  banked sample window 5
//
// The board is almost independent of the CPU board: its own ROM, RAM and clock,
// with a serial link (an i8251 on the host side talking to the SCSP's MIDI port)
// carrying the sound commands. There is no reset line from the host, so the
// sound program runs from power-on.
class SoundBoard final : public cpu::Bus68k, public ScspMemory {
public:
    SoundBoard();
    ~SoundBoard() override;

    SoundBoard(const SoundBoard&) = delete;
    SoundBoard& operator=(const SoundBoard&) = delete;

    // Sound program ROM (MAME's "audiocpu") and PCM sample ROM ("samples").
    // Either may be empty; a board with no program ROM is inert.
    void attach(std::span<const std::uint8_t> program_rom,
                std::span<const std::uint8_t> samples);

    void reset();

    // Applies the per-set flat SCSP gain that levels loudness across the
    // library. DOA's set name is "doa".
    void configure_balance(const std::string& game_name);

    // Advances the board by the sound-clock equivalent of `host_cycles` of the
    // host i960's 25 MHz clock. The two ratios (45.1584 MHz / 4 over 25 MHz for
    // the 68000, and 44100 Hz over 25 MHz for the SCSP) are kept exactly, with
    // remainders carried between calls, so nothing drifts.
    void run(std::uint32_t host_cycles);

    [[nodiscard]] bool present() const { return !m_program_rom.empty(); }

    // -- the serial link to the CPU board ------------------------------------

    // A command byte from the host's i8251, bound for the SCSP's MIDI port.
    void midi_in(std::uint8_t value);
    void set_midi_out_handler(Scsp::MidiOutHandler handler);

    // -- audio ---------------------------------------------------------------

    // Interleaved stereo samples at 44100 Hz produced since the last clear.
    [[nodiscard]] std::span<const std::int16_t> pending_samples() const { return m_pending; }
    void clear_pending_samples() { m_pending.clear(); }
    [[nodiscard]] std::uint32_t sample_rate() const { return m_scsp.sample_rate(); }

    // SCSP slots currently keyed on, for the status line.
    [[nodiscard]] std::uint32_t active_voices() const { return m_scsp.active_slots(); }

    // -- cpu::Bus68k ---------------------------------------------------------
    // The 68000's bus is big-endian, unlike every other bus in the machine.

    std::uint8_t read8(std::uint32_t address) override;
    std::uint16_t read16(std::uint32_t address) override;
    std::uint32_t read32(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;

    // -- ScspMemory ----------------------------------------------------------
    // The SCSP addresses the same 512 KB the 68000 uses as work RAM.

    std::uint8_t scsp_read_byte(std::uint32_t address) override;
    std::uint16_t scsp_read_word(std::uint32_t address) override;
    void scsp_write_word(std::uint32_t address, std::uint16_t value) override;

    // -- inspection ----------------------------------------------------------

    struct Counters {
        std::uint64_t scsp_reads = 0;
        std::uint64_t scsp_writes = 0;
        std::uint64_t snd_ctrl_writes = 0;
        std::uint64_t sample_reads = 0;
        std::uint64_t unmapped_reads = 0;
        std::uint64_t unmapped_writes = 0;
        std::uint64_t samples_dropped = 0;
    };
    [[nodiscard]] const Counters& counters() const { return m_counters; }
    [[nodiscard]] const cpu::M68000& cpu() const { return m_cpu; }
    [[nodiscard]] const Scsp& scsp() const { return m_scsp; }
    [[nodiscard]] std::span<const std::uint8_t> ram() const { return m_ram; }

private:
    // Resolves a 68000 address to a byte pointer, or null for anything that is
    // not plain memory. `writable` is false for ROM.
    struct Window {
        std::uint8_t* base = nullptr;
        std::size_t size = 0;
        bool writable = false;
    };
    [[nodiscard]] Window resolve(std::uint32_t address);

    void snd_ctrl_write(std::uint16_t value);

    // Charges the 68000 the wait states RAM and the SCSP impose on an access.
    void bus_wait(std::uint32_t address);

    // Generates the SCSP's next sample into m_pending.
    void generate_sample();

    cpu::M68000 m_cpu;

    // Constructed with *this as its memory; the constructor only stores the
    // pointer, so this is safe in the initialiser list.
    Scsp m_scsp;

    std::span<const std::uint8_t> m_program_rom;
    std::span<const std::uint8_t> m_samples;

    std::vector<std::uint8_t> m_ram;  // 512 KB, shared with the SCSP.

    std::uint32_t m_bank4_offset = 0x200000;
    std::uint32_t m_bank5_offset = 0x600000;

    std::uint64_t m_cycle_debt = 0;  // Numerator carried between run() calls.
    std::int64_t m_cycle_budget = 0;  // 68000 cycles granted but not yet run.
    std::uint64_t m_sample_phase = 0;  // Cycles into the most recent sample.

    std::vector<std::int16_t> m_pending;

    Counters m_counters;
    bool m_warned_no_program = false;
};

}  // namespace sage::hw
