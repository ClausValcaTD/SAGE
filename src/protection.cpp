// Sega 315-5838 / 317-0229 "comp" device.
//
// Ported from MAME's src/mame/sega/315-5838_317-0229_comp.cpp (BSD-3-Clause,
// copyright-holders: David Haywood, Samuel Neves, Peter Wilhelmsen, Morten
// Shearman Kirkegaard). See include/sage/protection.h for the interface notes.

#include "sage/protection.h"

#include <algorithm>

namespace sage {
namespace {

constexpr bool bit(std::uint32_t v, int n) { return (v >> n) & 1; }

struct CipherParameters {
    std::uint8_t routing[4];
    std::uint8_t sboxes[4][8];
    std::uint8_t affine[4];
};

// Routing, substitutions and affine masks for every variant.
constexpr CipherParameters kCiphers[4] = {
    // 315-5838
    {{0, 1, 2, 3},
     {{6, 7, 2, 0, 5, 1, 3, 4}, {5, 3, 7, 2, 1, 6, 4, 0}, {2, 0, 6, 1, 3, 7, 4, 5},
      {6, 5, 7, 4, 1, 3, 0, 2}},
     {0b10100, 0b11000, 0b00010, 0b10101}},
    // 317-0229
    {{3, 2, 0, 1},
     {{6, 0, 5, 4, 7, 2, 1, 3}, {5, 6, 1, 3, 7, 0, 4, 2}, {2, 6, 0, 1, 3, 4, 7, 5},
      {6, 5, 7, 4, 1, 3, 0, 2}},
     {0b10010, 0b10001, 0b00100, 0b11010}},
    // 317-0230
    {{2, 3, 1, 0},
     {{6, 2, 7, 0, 5, 3, 1, 4}, {5, 3, 7, 2, 1, 6, 4, 0}, {2, 1, 3, 5, 6, 0, 4, 7},
      {6, 1, 3, 5, 7, 0, 2, 4}},
     {0b10100, 0b11000, 0b00010, 0b10101}},
    // 317-0231
    {{1, 0, 3, 2},
     {{6, 0, 5, 4, 7, 2, 1, 3}, {5, 1, 6, 3, 7, 4, 0, 2}, {2, 1, 3, 5, 6, 0, 4, 7},
      {6, 3, 1, 5, 7, 2, 0, 4}},
     {0b10010, 0b10001, 0b00100, 0b11010}},
};

constexpr std::uint8_t kOutputBits[4][3] = {{0, 1, 7}, {2, 5, 9}, {4, 10, 13}, {6, 11, 15}};
constexpr std::uint8_t kAffineBits[4] = {3, 8, 12, 14};

}  // namespace

Sega3155838Comp::Sega3155838Comp(Variant variant) : m_variant(variant) { reset(); }

void Sega3155838Comp::reset() {
    m_tree.fill(0);
    m_dictionary.fill(0);
    m_tree_words = 0;
    m_dictionary_bytes = 0;
    m_upload_dictionary = false;
    m_source = 0;
    m_word = 0;
    m_bits = 0;
    m_output = 0xffff;
    m_abort = false;
}

std::uint16_t Sega3155838Comp::decipher(std::uint16_t ciphertext) const {
    const auto x = [ciphertext](unsigned b) { return bit(ciphertext, b); };

    const std::uint8_t lanes[4] = {
        static_cast<std::uint8_t>((x(5) ^ x(12)) | ((x(7) ^ x(9)) << 1) | (x(14) << 2)),
        static_cast<std::uint8_t>(x(6) | (x(8) << 1) | (x(15) << 2)),
        static_cast<std::uint8_t>((x(1) ^ x(8)) | (x(7) << 1) | (x(12) << 2)),
        static_cast<std::uint8_t>(x(3) | (x(4) << 1) | (x(10) << 2))};

    const std::uint8_t affine =
        static_cast<std::uint8_t>(x(0) | ((x(0) ^ x(2)) << 1) | ((x(11) ^ x(14)) << 2) |
                                  ((x(4) ^ x(13)) << 3) | 16);

    const CipherParameters& cipher = kCiphers[static_cast<unsigned>(m_variant)];
    std::uint16_t result = 0;

    for (unsigned group = 0; group < 4; ++group) {
        const std::uint8_t value = cipher.sboxes[group][lanes[cipher.routing[group]]];
        for (unsigned b = 0; b < 3; ++b)
            result |= bit(value, b) << kOutputBits[group][b];

        std::uint8_t parity = affine & cipher.affine[group];
        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;
        result |= bit(parity, 0) << kAffineBits[group];
    }

    return result;
}

void Sega3155838Comp::source_w(std::uint32_t data) {
    m_source = data & 0x007fffff;
    m_word = 0;
    m_bits = 0;
    m_abort = false;
}

void Sega3155838Comp::table_w(unsigned offset, std::uint16_t data) {
    if (!(offset & 1)) {
        m_upload_dictionary = bit(data, 7);
        if (m_upload_dictionary)
            m_dictionary_bytes = 0;
        else
            m_tree_words = 0;
    } else if (m_upload_dictionary) {
        if (m_dictionary_bytes < m_dictionary.size()) {
            m_dictionary[m_dictionary_bytes++] = static_cast<std::uint8_t>(data >> 8);
            m_dictionary[m_dictionary_bytes++] = static_cast<std::uint8_t>(data & 0xff);
        }
    } else if (m_tree_words < m_tree.size()) {
        m_tree[m_tree_words++] = data;
    }
}

std::uint8_t Sega3155838Comp::decompress_byte() {
    if (m_abort || m_tree_words != m_tree.size() || m_dictionary_bytes != m_dictionary.size())
        return 0xff;

    std::uint16_t code = 0;
    for (unsigned length = 1; length <= 12; ++length) {
        if (!m_bits) {
            m_word = decipher(m_source_cb(m_source));
            m_source = (m_source + 1) & 0x007fffff;
            m_bits = 16;
        }
        code = static_cast<std::uint16_t>((code << 1) | bit(m_word, --m_bits));

        for (unsigned entry = 0; entry < m_tree.size(); entry += 2) {
            if (((m_tree[entry] >> 8) & 0xff) != length)
                continue;

            const std::uint16_t first = m_tree[entry + 1] >> (12 - length);
            std::uint16_t limit = static_cast<std::uint16_t>(1U << length);
            for (unsigned next = entry + 2; next < m_tree.size(); next += 2) {
                if (m_tree[next + 1] != m_tree[entry + 1]) {
                    limit = m_tree[next + 1] >> (12 - length);
                    break;
                }
            }

            if (code >= first && code < limit) {
                const unsigned index = (m_tree[entry] & 0xff) + code - first;
                if (index < m_dictionary.size())
                    return m_dictionary[index];
            }
        }
    }

    // Invalid tables or data must not loop forever or read past the dictionary.
    m_abort = true;
    return 0xff;
}

std::uint16_t Sega3155838Comp::data_r() {
    m_output = static_cast<std::uint16_t>(decompress_byte()) << 8;
    m_output |= decompress_byte();
    return m_output;
}

}  // namespace sage
