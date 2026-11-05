#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace sage {

// Sega 315-5649 I/O controller.
//
// Ported from MAME's src/mame/sega/315_5649.cpp (BSD-3-Clause,
// copyright-holders: Dirk Best). Model 2 maps it byte-wise at 0x01c00000 with
// the register index in address bits 8..15 (umask32 0x00ff00ff), so a 32-bit
// word access A0..A1 selects a register pair and byte lane 0/2 picks the byte.
//
// Ports A..G are individually configurable as input or output via register 8;
// the Model 2B board uses A for the EEPROM bit-bang, B for the coin/service
// inputs, C/D for the player controls, G for the DIP switches, and E/F as lamp
// outputs.
class Io3155649 {
public:
    using InPortFn = std::function<std::uint8_t(int port)>;
    using OutPortFn = std::function<void(int port, std::uint8_t data)>;
    using AnalogFn = std::function<std::uint8_t(int channel)>;
    using CounterFn = std::function<std::uint16_t(int counter)>;
    using SerialRdFn = std::function<std::uint8_t(int channel)>;
    using SerialWrFn = std::function<void(int channel, std::uint8_t data)>;

    Io3155649();

    void reset();

    void set_in_port(InPortFn fn) { m_in = std::move(fn); }
    void set_out_port(OutPortFn fn) { m_out = std::move(fn); }
    void set_analog(AnalogFn fn) { m_analog = std::move(fn); }
    void set_counter(CounterFn fn) { m_counter = std::move(fn); }
    void set_serial_rd(SerialRdFn fn) { m_serial_rd = std::move(fn); }
    void set_serial_wr(SerialWrFn fn) { m_serial_wr = std::move(fn); }

    std::uint8_t read(unsigned reg);
    void write(unsigned reg, std::uint8_t data);

private:
    std::array<std::uint8_t, 7> m_port_value{};
    std::uint8_t m_port_config = 0xff;  // 0 = output, 1 = input
    std::uint8_t m_mode = 0;
    int m_analog_channel = 0;

    InPortFn m_in;
    OutPortFn m_out;
    AnalogFn m_analog;
    CounterFn m_counter;
    SerialRdFn m_serial_rd;
    SerialWrFn m_serial_wr;
};

}  // namespace sage
