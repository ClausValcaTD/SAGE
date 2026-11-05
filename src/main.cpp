#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sage/bus.h"
#include "sage/cpu/i960.h"
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
        std::printf("  %-18s %-18s %10s  %08" PRIX32 "  %s\n", r.entry_name.c_str(),
                    r.archive_name.empty() ? "-" : r.archive_name.c_str(),
                    human_size(r.size).c_str(), r.actual_crc, status);
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
        std::printf("  %-12s %12s  %4u-bit  %-8s  %08" PRIX32 "\n", name.c_str(),
                    human_size(r ? r->data.size() : 0).c_str(), r ? r->bus_width : 0,
                    (r && r->big_endian) ? "big" : "little", crc);
    }
    std::printf("------------------------------------------------------------\n\n");
}

void print_boot_state(const sage::cpu::I960& cpu) {
    std::printf("i960 boot vector (read at reset from 0x00000000)\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  SAT  (0x00)     : 0x%08" PRIX32 "\n", cpu.get_sat());
    std::printf("  PRCB (0x04)     : 0x%08" PRIX32 "\n", cpu.get_prcb());
    std::printf("  Entry IP (0x0C) : 0x%08" PRIX32 "\n", cpu.get_ip());
    std::printf("  PC (control)    : 0x%08" PRIX32 "\n", cpu.get_pc());
    std::printf("  AC (condition)  : 0x%08" PRIX32 "\n", cpu.get_ac());
    std::printf("  FP (g15)        : 0x%08" PRIX32 "\n", cpu.reg(31));
    std::printf("  SP (r1)         : 0x%08" PRIX32 "\n", cpu.reg(1));
    std::printf("------------------------------------------------------------\n\n");
}

}  // namespace

int main(int argc, char** argv) {
    print_banner();

    const std::string zip_path = (argc > 1) ? argv[1] : "roms/doa.zip";
    const std::string game = (argc > 2) ? argv[2] : "doa";
    const int max_instructions = (argc > 3) ? std::atoi(argv[3]) : 1000;

    std::printf("Loading ROM set: %s\n\n", zip_path.c_str());

    sage::RomLoader loader;
    if (!loader.load(zip_path, game)) {
        std::printf("ERROR: %s\n", loader.report().error.c_str());
        return 1;
    }

    const sage::LoadReport& rep = loader.report();
    print_rom_table(rep);
    print_region_table(loader, rep);

    const sage::Region* cpu_region = loader.region("maincpu");
    const std::size_t cpu_end = cpu_region ? cpu_region->data.size() - 1 : 0;

    std::printf("Load summary\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  Game            : %s\n", rep.game.c_str());
    std::printf("  Revision        : %s\n", rep.revision.c_str());
    std::printf("  Hardware        : %s\n", rep.hardware.c_str());
    std::printf("  ROM archive     : %s\n", zip_path.c_str());
    std::printf("  Regions mapped  : %zu\n", rep.region_crcs.size());
    std::printf("  Total ROM bytes : %s\n", human_size(rep.total_bytes).c_str());
    std::printf("  Program memory  : 0x00000000 - 0x%08zX (%s)\n", cpu_end,
                human_size(rep.program_bytes).c_str());
    std::printf("------------------------------------------------------------\n\n");

    // ---- CPU bring-up -----------------------------------------------------
    sage::Bus bus;
    bus.attach(loader);

    sage::cpu::I960 cpu(bus);
    cpu.reset();

    print_boot_state(cpu);

    std::printf("Execution trace (%d instructions)\n", max_instructions);
    std::printf("------------------------------------------------------------\n");

    std::uint64_t cycles = 0;
    int executed = 0;
    bool faulted = false;
    std::string fault_msg;

    for (int i = 0; i < max_instructions; i++) {
        const std::uint32_t pc = cpu.get_ip();
        const std::uint32_t opcode = bus.read32(pc);

        if (i < 20) {
            const std::string text = cpu.disassemble(pc);
            std::printf("  [0x%08" PRIX32 "] %08" PRIX32 "  %-26s (g0=%08" PRIX32 " r0=%08" PRIX32
                        " ac=%08" PRIX32 ")\n",
                        pc, opcode, text.c_str(), cpu.reg(16), cpu.reg(0), cpu.get_ac());
        }

        int used;
        try {
            used = cpu.step();
        } catch (const sage::cpu::Fault& f) {
            std::printf("  [0x%08" PRIX32 "] %08" PRIX32 "  FAULT: %s\n", pc, opcode, f.what());
            fault_msg = f.what();
            faulted = true;
            break;
        }

        cycles += static_cast<std::uint64_t>(used);
        executed++;

        if ((i + 1) % 200 == 0)
            std::printf("  -- checkpoint: %d instructions, %" PRIu64 " cycles, IP=0x%08" PRIX32 "\n",
                        i + 1, cycles, cpu.get_ip());
    }

    std::printf("------------------------------------------------------------\n\n");

    std::printf("Execution summary\n");
    std::printf("------------------------------------------------------------\n");
    std::printf("  Instructions    : %d\n", executed);
    std::printf("  Cycles          : %" PRIu64 "\n", cycles);
    std::printf("  Final IP        : 0x%08" PRIX32 "\n", cpu.get_ip());
    std::printf("  Unmapped reads  : %" PRIu64 "\n", bus.unmapped_reads);
    std::printf("  Unmapped writes : %" PRIu64 "\n", bus.unmapped_writes);
    std::printf("  ROM writes      : %" PRIu64 "\n", bus.rom_writes);
    std::printf("------------------------------------------------------------\n");
    if (faulted)
        std::printf("  doa.zip mapped and executed; CPU halted on an unimplemented opcode:\n    %s\n",
                    fault_msg.c_str());
    else
        std::printf("  doa.zip mapped into simulated address space.\n  Ready for CPU execution.\n");
    return faulted ? 2 : 0;
}
