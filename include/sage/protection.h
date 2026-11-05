#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace sage {

// Sega 315-5838 / 317-0229 "comp" device.
//
// Ported from MAME's src/mame/sega/315-5838_317-0229_comp.cpp (BSD-3-Clause,
// copyright-holders: David Haywood, Samuel Neves, Peter Wilhelmsen, Morten
// Shearman Kirkegaard). Dead or Alive's 837-12880 security board uses the
// 317-0229 variant to encode a 48-character startup string; the i960 reads the
// decoded bytes back one halfword at a time and compares them.
//
// Word transform -> programmable prefix decompression, driven by two uploads:
// a 24-halfword code tree and a 256-byte dictionary. The encrypted source words
// are fetched through a callback supplied by the bus (DOA feeds them from its
// 0x01d80000 RAM window).
class Sega3155838Comp {
public:
    enum class Variant { Sega3155838, Sega3170229, Sega3170230, Sega3170231 };

    using SourceFn = std::function<std::uint16_t(std::uint32_t word_index)>;

    explicit Sega3155838Comp(Variant variant = Variant::Sega3170229);

    void reset();
    void set_source(SourceFn fn) { m_source_cb = std::move(fn); }

    // i960 writes the current source word index here.
    void source_w(std::uint32_t data);

    // Table upload port: offset bit 0 selects mode; odd offsets push payload.
    void table_w(unsigned offset, std::uint16_t data);

    // Decodes two bytes, high byte first, as one halfword.
    std::uint16_t data_r();

private:
    static constexpr std::size_t kTreeWords = 24;
    static constexpr std::size_t kDictionaryBytes = 256;

    std::uint16_t decipher(std::uint16_t ciphertext) const;
    std::uint8_t decompress_byte();

    Variant m_variant;
    SourceFn m_source_cb;

    std::array<std::uint16_t, kTreeWords> m_tree{};
    std::array<std::uint8_t, kDictionaryBytes> m_dictionary{};
    std::size_t m_tree_words = 0;
    std::size_t m_dictionary_bytes = 0;
    bool m_upload_dictionary = false;

    std::uint32_t m_source = 0;
    std::uint16_t m_word = 0;
    int m_bits = 0;
    std::uint16_t m_output = 0xffff;
    bool m_abort = false;
};

}  // namespace sage
