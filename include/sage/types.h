#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sage {

// A loaded ROM region: a contiguous block of simulated address space whose
// contents were assembled by replaying MAME's ROM_LOAD entries in order.
struct Region {
    std::string name;                 // MAME region tag, e.g. "maincpu"
    std::vector<std::uint8_t> data;   // region contents (post endianness fixup)
    std::uint32_t bus_width = 8;      // native region width in bits (8/16/32)
    bool big_endian = false;          // region endianness flag from ROM_REGIONxx
    std::uint8_t erase_value = 0x00;  // fill byte used before any ROM is loaded
};

// Result of verifying one ROM_LOAD entry against the archive.
struct RomVerify {
    std::string entry_name;    // filename MAME expects
    std::string archive_name;  // filename actually found in the zip
    std::uint32_t expected_crc = 0;
    std::uint32_t actual_crc = 0;
    std::uint32_t alt_crc = 0;  // known alternate dump (0 = none)
    std::size_t size = 0;
    bool found = false;
    bool crc_ok = false;  // matches the expected (MAME) dump
    bool alt_ok = false;  // matches a known alternate dump
};

// Outcome of a whole-cartridge load.
struct LoadReport {
    std::string game;
    std::string hardware;
    std::string revision;
    std::vector<RomVerify> roms;
    std::vector<std::pair<std::string, std::uint32_t>> region_crcs;
    std::size_t program_bytes = 0;
    std::size_t total_bytes = 0;
    bool ok = false;
    std::string error;
};

}  // namespace sage
