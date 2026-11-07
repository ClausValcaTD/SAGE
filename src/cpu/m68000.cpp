// Motorola 68EC000 sound CPU.
//
// See include/sage/cpu/m68000.h. The core is Musashi (Karl Stenerud, MIT); this
// file is the glue between its global memory callbacks and a cpu::Bus68k.

#include "sage/cpu/m68000.h"

#include <cstdio>

extern "C" {
#include <m68k.h>
}

namespace {

// Musashi reaches memory through process-global callbacks. SAGE drives one
// sound CPU at a time, so a single bus pointer is installed for its lifetime.
sage::cpu::Bus68k* g_bus = nullptr;
bool g_inited = false;

}  // namespace

namespace sage::cpu {

M68000::M68000(Bus68k& bus) : m_bus(&bus) {
    if (!g_inited) {
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        g_inited = true;
    }
    g_bus = m_bus;
}

M68000::~M68000() {
    if (g_bus == m_bus)
        g_bus = nullptr;
}

void M68000::reset() {
    m_irq_mask = 0;
    m_total_cycles = 0;
    m68k_pulse_reset();
    apply_irq();
}

int M68000::run(int cycles) {
    if (cycles <= 0)
        return 0;

    // Musashi drops the interrupt level when it takes the exception, so a level
    // that is still being held has to be put back before each slice.
    apply_irq();

    m_running = true;
    m_stalled = 0;
    // Separate statement: stall() updates m_stalled during the call.
    const int executed = m68k_execute(cycles);
    const int used = executed + m_stalled;
    m_running = false;
    m_total_cycles += static_cast<std::uint64_t>(used < 0 ? 0 : used);
    return used;
}

void M68000::stall(int cycles) {
    if (!m_running)
        return;
    // Shrinking the timeslice ends it that much sooner without counting towards
    // what Musashi reports as used, so run() adds the stalls back itself.
    m68k_modify_timeslice(-cycles);
    m_stalled += cycles;
}

void M68000::set_irq_line(int level, bool asserted) {
    if (level < 1 || level > 7)
        return;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << level);
    if (asserted)
        m_irq_mask |= bit;
    else
        m_irq_mask = static_cast<std::uint8_t>(m_irq_mask & ~bit);
    apply_irq();
}

void M68000::clear_irq_lines() {
    m_irq_mask = 0;
    apply_irq();
}

void M68000::apply_irq() const {
    // The three IPL pins encode the highest pending level, so a lower level
    // asserted at the same time is invisible until the higher one is released.
    unsigned int level = 0;
    for (int candidate = 7; candidate >= 1; --candidate) {
        if (m_irq_mask & (1u << candidate)) {
            level = static_cast<unsigned int>(candidate);
            break;
        }
    }
    m68k_set_irq(level);
}

std::uint32_t M68000::pc() const { return m68k_get_reg(nullptr, M68K_REG_PC); }
std::uint32_t M68000::sr() const { return m68k_get_reg(nullptr, M68K_REG_SR); }
std::uint32_t M68000::sp() const { return m68k_get_reg(nullptr, M68K_REG_SP); }

std::string M68000::state_string() const {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, "PC=%06X SR=%04X SP=%06X", pc(), sr(), sp());
    return buffer;
}

}  // namespace sage::cpu

// ---------------------------------------------------------------------------
// Musashi's memory interface
// ---------------------------------------------------------------------------
// M68K_SEPARATE_READS is off, so instruction and PC-relative fetches come
// through these same six functions. Reads before reset() return 0 rather than
// dereferencing null, because Musashi calls the read callbacks from
// m68k_pulse_reset to fetch the reset vector.

extern "C" {

unsigned int m68k_read_memory_8(unsigned int address) {
    return g_bus ? g_bus->read8(address) : 0;
}

unsigned int m68k_read_memory_16(unsigned int address) {
    return g_bus ? g_bus->read16(address) : 0;
}

unsigned int m68k_read_memory_32(unsigned int address) {
    return g_bus ? g_bus->read32(address) : 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    if (g_bus)
        g_bus->write8(address, static_cast<std::uint8_t>(value));
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    if (g_bus)
        g_bus->write16(address, static_cast<std::uint16_t>(value));
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    if (g_bus)
        g_bus->write32(address, static_cast<std::uint32_t>(value));
}

}  // extern "C"
