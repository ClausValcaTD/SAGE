// Sega 315-5649 I/O controller.
//
// Ported from MAME's src/mame/sega/315_5649.cpp (BSD-3-Clause,
// copyright-holders: Dirk Best).

#include "sage/io.h"

namespace sage {

Io3155649::Io3155649() { reset(); }

void Io3155649::reset() {
    m_port_value.fill(0xff);
    m_port_config = 0xff;
    m_mode = 0;
    m_analog_channel = 0;
}

std::uint8_t Io3155649::read(unsigned reg) {
    switch (reg) {
        case 0x06:
            if (m_mode & 0x80) {  // port G counter mode: 4x 16-bit counters
                std::uint8_t data = 0xff;
                if (m_counter) {
                    const std::uint16_t v = m_counter((m_port_value[6] >> 1) & 3);
                    data = static_cast<std::uint8_t>(v >> (((m_port_value[6] & 1) ^ 1) * 8));
                }
                m_port_value[6] = static_cast<std::uint8_t>((m_port_value[6] & 0xf8) |
                                                            ((m_port_value[6] + 1) & 7));
                return data;
            }
            [[fallthrough]];
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            if (m_port_config & (1u << reg))
                return m_in ? m_in(static_cast<int>(reg)) : 0xff;
            return m_port_value[reg];

        case 0x0b:  // RS-422 channel 1 input
            return m_serial_rd ? m_serial_rd(0) : 0;
        case 0x0c:  // RS-422 channel 2 input
            return m_serial_rd ? m_serial_rd(1) : 0;

        case 0x0d:  // RS-422 status: receive buffers full, transmit buffers empty
            return 0x0c;

        case 0x0f: {  // analog input, auto-increments
            const std::uint8_t data = m_analog ? m_analog(m_analog_channel) : 0xff;
            m_analog_channel = (m_analog_channel + 1) & 0x07;
            return data;
        }

        default:
            return 0xff;
    }
}

void Io3155649::write(unsigned reg, std::uint8_t data) {
    switch (reg) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
            m_port_value[reg] = data;
            if (m_out)
                m_out(static_cast<int>(reg), data);
            break;

        case 0x08:
            m_port_config = data;
            break;

        case 0x09:
            if (m_serial_wr)
                m_serial_wr(0, data);
            break;
        case 0x0a:
            if (m_serial_wr)
                m_serial_wr(1, data);
            break;

        case 0x0e:
            m_mode = data;
            break;

        case 0x0f:
            m_analog_channel = data & 0x07;
            break;

        default:
            break;
    }
}

}  // namespace sage
