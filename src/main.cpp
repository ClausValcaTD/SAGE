#include <cinttypes>
#include <cstdio>
#include <string>

#include "sage/rom_loader.h"

namespace {

constexpr const char* kBanner =
    "  ____    _    ____ _____ \n"
    " / ___|  / \\  / ___| ____|\n"
    " \\___ \\ / _ \\| |  _|  _|  \n"
    "  ___) / ___ \\ |_| | |___ \n"
    " |____/_/   \\_\\____|_____|\n";

void print_banner() {
    std::printf("%s", kBanner);
    std::printf(" SAGE - Sega Arcade Golden-era Emulator\n");
    std::printf(" Target: Dead or Alive - Sega Model 2B\n");
    std::printf("============================================================\n\n");
}

std::string human_size(std::size_t bytes) {
    char buf[64];
    if (bytes >= 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, sizeof(buf), "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%zu B", bytes);
    return buf;
}

void print_rom_table(const sage::LoadReport& rep) {
    std::printf("ROM IC verification\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  %-18s %-18s %10s  %-8s  %s\n", "IC (MAME)", "IC (archive)", "Size", "CRC32", "Status");
    std::printf("------------------------------------------------------------\n");
    std::size_t ok = 0, alt = 0;
    for (const auto& r : rep.roms) {
        const char* status = r.crc_ok ? "OK"
                           : r.alt_ok ? "OK (alt dump)"
                           : (r.found ? "CRC MISMATCH" : "MISSING");
        if (r.crc_ok)
            ++ok;
        else if (r.alt_ok)
            ++alt;
        std::printf("  %-18s %-18s %10s  %08" PRIX32 "  %s\n",
                    r.entry_name.c_str(),
                    r.archive_name.empty() ? "-" : r.archive_name.c_str(),
                    human_size(r.size).c_str(),
                    r.actual_crc,
                    status);
    }
    std::printf("------------------------------------------------------------\n");
    std::printf("  %zu / %zu ROM ICs verified", ok, rep.roms.size());
    if (alt)
        std::printf(" (%zu known alternate dump%s)", alt, alt == 1 ? "" : "s");
    std::printf("\n\n");
}

void print_region_table(const sage::RomLoader& loader, const sage::LoadReport& rep) {
    std::printf("Mapped memory regions\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  %-12s %12s  %6s  %-8s  %s\n", "Region", "Size", "Width", "Endian", "CRC32");
    std::printf("------------------------------------------------------------\n");
    for (const auto& [name, crc] : rep.region_crcs) {
        const sage::Region* r = loader.region(name);
        std::printf("  %-12s %12s  %4u-bit  %-8s  %08" PRIX32 "\n",
                    name.c_str(),
                    human_size(r ? r->data.size() : 0).c_str(),
                    r ? r->bus_width : 0,
                    (r && r->big_endian) ? "big" : "little",
                    crc);
    }
    std::printf("------------------------------------------------------------\n\n");
}

}  // namespace

int main(int argc, char** argv) {
    print_banner();

    const std::string zip_path = (argc > 1) ? argv[1] : "roms/doa.zip";
    const std::string game = (argc > 2) ? argv[2] : "doa";

    std::printf("Loading ROM set: %s\n\n", zip_path.c_str());

    sage::RomLoader loader;
    if (!loader.load(zip_path, game)) {
        std::printf("ERROR: %s\n", loader.report().error.c_str());
        return 1;
    }

    const sage::LoadReport& rep = loader.report();
    print_rom_table(rep);
    print_region_table(loader, rep);

    const sage::Region* cpu = loader.region("maincpu");
    const std::size_t cpu_end = cpu ? cpu->data.size() - 1 : 0;

    std::printf("Load summary\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  Game            : %s\n", rep.game.c_str());
    std::printf("  Revision        : %s\n", rep.revision.c_str());
    std::printf("  Hardware        : %s\n", rep.hardware.c_str());
    std::printf("  ROM archive     : %s\n", zip_path.c_str());
    std::printf("  Regions mapped  : %zu\n", rep.region_crcs.size());
    std::printf("  Total ROM bytes : %s\n", human_size(rep.total_bytes).c_str());
    std::printf("  Program memory  : 0x00000000 - 0x%08zX (%s)\n",
                cpu_end, human_size(rep.program_bytes).c_str());
    std::printf("------------------------------------------------------------\n");
    std::printf("  doa.zip mapped into simulated address space.\n");
    std::printf("  Ready for CPU execution.\n");
    return 0;
}
