// Serial 93C46 EEPROM.
//
// Ported from MAME's src/devices/machine/eepromser.cpp + eeprom.cpp
// (BSD-3-Clause, copyright-holders: Aaron Giles, Vas Crabb).

#include "sage/eeprom.h"

namespace sage {
namespace {

constexpr int kCsRising = 1 << 0;
constexpr int kCsFalling = 1 << 1;
constexpr int kClkRising = 1 << 2;
constexpr int kClkFalling = 1 << 3;

}  // namespace

void Eeprom93C46::reset() {
    m_data.fill(0xffff);
    m_state = State::InReset;
    m_cs_state = 0;
    m_clk_state = 0;
    m_di_state = 0;
    m_locked = true;
    m_bits_accum = 0;
    m_command_address_accum = 0;
    m_command = Command::Invalid;
    m_address = 0;
    m_shift_register = 0;
}

void Eeprom93C46::cs_write(int state) {
    state &= 1;
    if (state == m_cs_state)
        return;
    m_cs_state = state;
    handle_event(m_cs_state ? kCsRising : kCsFalling);
}

void Eeprom93C46::clk_write(int state) {
    state &= 1;
    if (state == m_clk_state)
        return;
    m_clk_state = state;
    handle_event(m_clk_state ? kClkRising : kClkFalling);
}

void Eeprom93C46::di_write(int state) { m_di_state = state & 1; }

int Eeprom93C46::do_read() const {
    // 93C46: DO doubles as READY/BUSY while waiting for a start bit; in every
    // other state it follows do_line_for_state().
    if (m_state == State::WaitForStartBit)
        return 1;  // ready() is always true here
    return do_line_for_state();
}

int Eeprom93C46::do_line_for_state() const {
    // Tristated (pulled high) except while clocking data out, where DO is the
    // MSB of the shift register.
    return (m_state == State::ReadingData) ? ((m_shift_register >> 31) & 1) : 1;
}

void Eeprom93C46::handle_event(int event) {
    switch (m_state) {
        case State::InReset:
            if (event == kCsRising)
                set_state(State::WaitForStartBit);
            break;

        case State::WaitForStartBit:
            if (event == kClkRising && m_di_state == 1) {
                m_command_address_accum = m_bits_accum = 0;
                set_state(State::WaitForCommand);
            } else if (event == kCsFalling) {
                set_state(State::InReset);
            }
            break;

        case State::WaitForCommand:
            if (event == kClkRising) {
                m_command_address_accum = (m_command_address_accum << 1) | m_di_state;
                if (++m_bits_accum == static_cast<std::uint32_t>(2 + kAddressBits))
                    execute_command();
            } else if (event == kCsFalling) {
                set_state(State::InReset);
            }
            break;

        case State::ReadingData:
            if (event == kClkRising) {
                const int bit_index = static_cast<int>(m_bits_accum++);
                if (bit_index % kDataBits == 0 && bit_index == 0)
                    m_shift_register = static_cast<std::uint32_t>(
                                           m_data[(m_address + m_bits_accum / kDataBits) &
                                                  (kCells - 1)])
                                       << (32 - kDataBits);
                else
                    m_shift_register = (m_shift_register << 1) | 1;
            } else if (event == kCsFalling) {
                set_state(State::InReset);
            }
            break;

        case State::WaitForData:
            if (event == kClkRising) {
                m_shift_register = (m_shift_register << 1) | m_di_state;
                if (++m_bits_accum == static_cast<std::uint32_t>(kDataBits))
                    execute_write_command();
            } else if (event == kCsFalling) {
                set_state(State::InReset);
            }
            break;

        case State::WaitForCompletion:
            if (event == kCsFalling)
                set_state(State::InReset);
            break;
    }
}

void Eeprom93C46::parse_command_and_address() {
    m_command = Command::Invalid;
    m_address = m_command_address_accum & ((1u << kAddressBits) - 1);

    switch (m_command_address_accum >> kAddressBits) {
        case 0:
            switch (m_address >> (kAddressBits - 2)) {
                case 0: m_command = Command::Lock; break;
                case 1: m_command = Command::WriteAll; break;
                case 2: m_command = Command::EraseAll; break;
                case 3: m_command = Command::Unlock; break;
            }
            m_address = 0;
            break;
        case 1: m_command = Command::Write; break;
        case 2: m_command = Command::Read; break;
        case 3: m_command = Command::Erase; break;
    }
}

void Eeprom93C46::execute_command() {
    parse_command_and_address();
    m_bits_accum = 0;

    switch (m_command) {
        case Command::Read:
            m_shift_register = 0;
            set_state(State::ReadingData);
            break;

        case Command::Write:
        case Command::WriteAll:
            m_shift_register = 0;
            set_state(State::WaitForData);
            break;

        case Command::Erase:
            if (m_locked) {
                set_state(State::InReset);
                break;
            }
            m_data[m_address] = 0xffff;
            set_state(State::WaitForCompletion);
            break;

        case Command::Lock:
            m_locked = true;
            set_state(State::InReset);
            break;

        case Command::Unlock:
            m_locked = false;
            set_state(State::InReset);
            break;

        case Command::EraseAll:
            if (m_locked) {
                set_state(State::InReset);
                break;
            }
            m_data.fill(0xffff);
            set_state(State::WaitForCompletion);
            break;

        case Command::Invalid:
        default:
            set_state(State::InReset);
            break;
    }
}

void Eeprom93C46::execute_write_command() {
    switch (m_command) {
        case Command::Write:
            if (m_locked) {
                set_state(State::InReset);
                break;
            }
            m_data[m_address] = static_cast<std::uint16_t>(m_shift_register);
            set_state(State::WaitForCompletion);
            break;

        case Command::WriteAll:
            if (m_locked) {
                set_state(State::InReset);
                break;
            }
            for (auto& cell : m_data)
                cell = static_cast<std::uint16_t>(cell & m_shift_register);
            set_state(State::WaitForCompletion);
            break;

        default:
            set_state(State::InReset);
            break;
    }
}

}  // namespace sage
