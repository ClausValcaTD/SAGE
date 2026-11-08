#pragma once

#include <cstdint>
#include <string>

namespace sage::cpu {

// Memory interconnect seen by the 68EC000 sound CPU. Unlike the i960's bus this
// one is big-endian: the sound board's read16/write16 are the primitives and the
// wider accesses are assembled from them.
class Bus68k {
public:
    virtual ~Bus68k() = default;
    virtual std::uint8_t read8(std::uint32_t addr) = 0;
    virtual std::uint16_t read16(std::uint32_t addr) = 0;
    virtual std::uint32_t read32(std::uint32_t addr) = 0;
    virtual void write8(std::uint32_t addr, std::uint8_t val) = 0;
    virtual void write16(std::uint32_t addr, std::uint16_t val) = 0;
    virtual void write32(std::uint32_t addr, std::uint32_t val) = 0;
};

// Motorola 68EC000 (68000 core) as it appears on the Model 2 sound board.
//
// The core itself is Musashi (Karl Stenerud, MIT); this class is only the glue
// between Musashi's global memory callbacks and a Bus68k. Musashi is a fully
// documented commodity part and is designed to be driven through memory
// callbacks, so nothing is lost by not tracking MAME's 68000 here.
//
// SAGE has a single sound CPU, so the multi-instance context swapping the
// sm2-emu reference needs is not reproduced; the bus pointer is simply
// installed for the life of the instance.
class M68000 {
public:
    explicit M68000(Bus68k& bus);
    ~M68000();

    M68000(const M68000&) = delete;
    M68000& operator=(const M68000&) = delete;

    // Takes the reset exception: SSP from address 0, PC from address 4. The
    // sound board seeds those 16 bytes of RAM from the start of the sound ROM
    // before this is called.
    void reset();

    // Executes until at least `cycles` have been consumed. Returns the number
    // actually used, which may exceed the request: instructions are not
    // interruptible.
    int run(int cycles);

    // Charges wait-state cycles to the instruction in flight. Only meaningful
    // from inside a bus callback during run().
    void stall(int cycles);

    // Assert or release one of the seven interrupt levels. Level-triggered:
    // Musashi clears the level when it services the exception, so it is
    // re-applied before every run().
    void set_irq_line(int level, bool asserted);

    // Release every interrupt level at once.
    void clear_irq_lines();

    std::uint32_t pc() const;
    std::uint32_t sr() const;
    std::uint32_t sp() const;

    std::uint64_t cycles() const { return m_total_cycles; }

    // One-line register dump.
    std::string state_string() const;

private:
    void apply_irq() const;

    Bus68k* m_bus = nullptr;
    std::uint8_t m_irq_mask = 0;  // bit n set: level n asserted
    bool m_running = false;
    int m_stalled = 0;
    std::uint64_t m_total_cycles = 0;
};

}  // namespace sage::cpu
