#pragma once

#include <array>
#include <cstdint>

namespace sage {

// Serial 93C46 EEPROM (64 x 16-bit cells), the part Model 2 wires to the
// 315-5649 I/O controller's port A. Dead or Alive bit-bangs a READ command at
// boot and spins on DO, so this must answer.
//
// Ported from MAME's src/devices/machine/eepromser.cpp + eeprom.cpp
// (BSD-3-Clause, copyright-holders: Aaron Giles, Vas Crabb). Internal
// write/erase completion delays are omitted: ready() is always true, which is
// indistinguishable to software that only reads.
class Eeprom93C46 {
public:
    static constexpr int kAddressBits = 6;
    static constexpr int kDataBits = 16;
    static constexpr int kCells = 1 << kAddressBits;

    Eeprom93C46() { reset(); }

    void reset();

    // Bit-banged interface.
    void cs_write(int state);
    void clk_write(int state);
    void di_write(int state);
    int do_read() const;

    bool locked() const { return m_locked; }
    std::uint16_t cell(int address) const { return m_data[address & (kCells - 1)]; }

private:
    enum class State { InReset, WaitForStartBit, WaitForCommand, ReadingData, WaitForData, WaitForCompletion };
    enum class Command { Invalid, Read, Write, Erase, Lock, Unlock, WriteAll, EraseAll };

    int do_line_for_state() const;
    void handle_event(int event);
    void set_state(State s) { m_state = s; }
    void parse_command_and_address();
    void execute_command();
    void execute_write_command();

    std::array<std::uint16_t, kCells> m_data{};

    State m_state = State::InReset;
    int m_cs_state = 0;
    int m_clk_state = 0;
    int m_di_state = 0;
    bool m_locked = true;
    std::uint32_t m_bits_accum = 0;
    std::uint32_t m_command_address_accum = 0;
    Command m_command = Command::Invalid;
    std::uint32_t m_address = 0;
    std::uint32_t m_shift_register = 0;
};

}  // namespace sage
