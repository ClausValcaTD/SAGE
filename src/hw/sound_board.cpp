// Model 2 sound board. See include/sage/hw/sound_board.h for the memory map and
// the notes on why the board runs independently of the CPU board.

#include "sage/hw/sound_board.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <unordered_map>

namespace sage::hw {

// ---------------------------------------------------------------------------
// Logging sink for the ported cores
// ---------------------------------------------------------------------------

void sound_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

namespace {

// ---------------------------------------------------------------------------
// Memory map (MAME's model2_snd, region for region)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kAddressMask = 0x00ffffff;  // The 68000 has 24 address lines.

constexpr std::uint32_t kRamBase = 0x000000;
constexpr std::uint32_t kRamSize = 0x080000;  // 512 KB, shared with the SCSP
constexpr std::uint32_t kScspBase = 0x100000;
constexpr std::uint32_t kScspSize = 0x001000;
constexpr std::uint32_t kSndCtrl = 0x400000;  // sample banking, write only
constexpr std::uint32_t kProgramBase = 0x600000;
constexpr std::uint32_t kProgramSize = 0x080000;
constexpr std::uint32_t kSamplesBase = 0x800000;  // samples + 0x000000, 2 MB
constexpr std::uint32_t kSamplesSize = 0x200000;
constexpr std::uint32_t kBank4Base = 0xa00000;  // samples + 0x200000, 4 MB
constexpr std::uint32_t kBank4Size = 0x400000;
constexpr std::uint32_t kBank5Base = 0xe00000;  // samples + 0x600000, 2 MB
constexpr std::uint32_t kBank5Size = 0x200000;

// The SCSP's own view of memory is 20 bits wide, of which the Model 2 sound
// board wires up the low 512 KB.
constexpr std::uint32_t kScspAddressMask = 0x000fffff;

// 68000 clock over host clock, exactly. 45.1584 MHz / 4 over 25 MHz reduces to
// 7056/15625, which is lowest terms, so the remainder has to be carried rather
// than rounded away.
constexpr std::uint64_t kCpuClockNumerator = 7056;
constexpr std::uint64_t kCpuClockDenominator = 15625;

// The 68000 and the SCSP share the 45.1584 MHz crystal, so one 44100 Hz sample
// is exactly 256 68000 cycles.
constexpr std::uint64_t kCyclesPerSample = 256;

// Wait states on a 68000 access to RAM or the SCSP: a fixed two, plus up to
// kContentionCycles more as the SCSP's own sample fetches take the RAM bus.
constexpr std::uint32_t kBusWaitCycles = 2;
constexpr std::uint32_t kContentionCycles = 12;

// The SCSP is clocked at half the 45.1584 MHz crystal on the video board, which
// divided by 512 is 44100 Hz exactly.
constexpr std::uint32_t kScspClock = 22'579'200;

// Longest run of audio kept while nothing is draining it, in frames. Four video
// frames is far more than a healthy consumer needs and small enough that a
// headless run does not accumulate a gigabyte of samples.
constexpr std::size_t kMaxPendingFrames = 4 * 800;

// The reset vector, 16 bytes, is copied from the start of the sound ROM into
// RAM. MAME's reset_model2_scsp does the same.
constexpr std::size_t kResetVectorBytes = 16;

}  // namespace

SoundBoard::SoundBoard() : m_cpu(*this), m_scsp(*this, kScspClock) {
    m_ram.assign(kRamSize, 0);
    m_pending.reserve(kMaxPendingFrames * 2);

    // The SCSP's interrupts go to the sound 68000 and only there: on Model 2 the
    // main_irq callback, which on a Saturn would reach the SH-2, is left unwired.
    m_scsp.set_irq_handler([this](int level, bool assert) {
        if (level <= 0) {
            m_cpu.clear_irq_lines();
            return;
        }
        m_cpu.set_irq_line(level, assert);
    });
}

SoundBoard::~SoundBoard() = default;

void SoundBoard::attach(std::span<const std::uint8_t> program_rom,
                        std::span<const std::uint8_t> samples) {
    m_program_rom = program_rom;
    m_samples = samples;
}

void SoundBoard::set_midi_out_handler(Scsp::MidiOutHandler handler) {
    m_scsp.set_midi_out_handler(std::move(handler));
}

void SoundBoard::midi_in(std::uint8_t value) { m_scsp.midi_in(value); }

void SoundBoard::reset() {
    std::fill(m_ram.begin(), m_ram.end(), std::uint8_t{0});

    m_bank4_offset = 0x200000;
    m_bank5_offset = 0x600000;

    m_cycle_debt = 0;
    m_cycle_budget = 0;
    m_sample_phase = kCyclesPerSample;
    m_counters = Counters{};
    m_pending.clear();

    m_scsp.reset();

    // Seed the vector table. Without this the 68000 would fetch its stack
    // pointer and entry point out of cleared RAM and run off into nothing.
    const std::size_t copy = std::min(kResetVectorBytes, m_program_rom.size());
    if (copy != 0)
        std::copy_n(m_program_rom.begin(), copy, m_ram.begin());

    m_cpu.reset();
}

void SoundBoard::configure_balance(const std::string& game_name) {
    // Per-set flat SCSP gain, in 1/256 units (256 == unity), levelling each
    // set's in-game loudness. Only DOA's entry is reproduced here; the map is
    // the reference's, keyed by MAME set name.
    static const std::unordered_map<std::string, std::uint16_t> kFlatGain = {
        {"doa", 500}, {"doab", 500},
    };
    const auto it = kFlatGain.find(game_name);
    if (it != kFlatGain.end()) {
        std::array<std::uint16_t, 32> gains;
        gains.fill(it->second);
        m_scsp.set_slot_gains(gains.data());
    }
}

void SoundBoard::run(std::uint32_t host_cycles) {
    if (!present()) {
        if (!m_warned_no_program) {
            m_warned_no_program = true;
            SM2_WARN("sound: no 'audiocpu' region, the sound board will stay silent");
        }
        return;
    }

    m_cycle_debt += static_cast<std::uint64_t>(host_cycles) * kCpuClockNumerator;
    m_cycle_budget += static_cast<std::int64_t>(m_cycle_debt / kCpuClockDenominator);
    m_cycle_debt %= kCpuClockDenominator;

    // The 68000 runs a sample at a time with the SCSP one sample ahead, so a
    // timer interrupts at the start of the sample it expires in and a reload
    // counts from the next one. The sequencers' tempo depends on it.
    while (m_cycle_budget > 0) {
        if (m_sample_phase >= kCyclesPerSample) {
            generate_sample();
            m_sample_phase -= kCyclesPerSample;
            continue;
        }
        const std::int64_t slice =
            std::min<std::int64_t>(m_cycle_budget,
                                   static_cast<std::int64_t>(kCyclesPerSample - m_sample_phase));
        const int used = m_cpu.run(static_cast<int>(slice));
        m_sample_phase += static_cast<std::uint64_t>(used);
        m_cycle_budget -= used;
    }

    // Nothing draining the buffer means a headless run. The SCSP still has to be
    // stepped, because that is where its timers and envelopes advance, so the
    // samples are generated and then the oldest are dropped.
    const std::size_t limit = kMaxPendingFrames * 2;
    if (m_pending.size() > limit) {
        const std::size_t excess = m_pending.size() - limit;
        m_counters.samples_dropped += excess / 2;
        m_pending.erase(m_pending.begin(),
                        m_pending.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}

void SoundBoard::generate_sample() {
    const std::size_t offset = m_pending.size();
    m_pending.resize(offset + 2);
    m_scsp.generate(m_pending.data() + offset, 1);
}

// ---------------------------------------------------------------------------
// Address decode
// ---------------------------------------------------------------------------

SoundBoard::Window SoundBoard::resolve(std::uint32_t address) {
    const auto sample_window = [this](std::uint32_t offset, std::uint32_t span) -> Window {
        if (offset >= m_samples.size())
            return {};
        Window result;
        result.base = const_cast<std::uint8_t*>(m_samples.data()) + offset;
        result.size = std::min<std::size_t>(span, m_samples.size() - offset);
        result.writable = false;
        return result;
    };

    if (address < kRamBase + kRamSize)
        return Window{m_ram.data() + address, kRamSize - address, true};

    if (address >= kProgramBase && address < kProgramBase + kProgramSize) {
        const std::uint32_t offset = address - kProgramBase;
        if (offset >= m_program_rom.size())
            return {};
        return Window{const_cast<std::uint8_t*>(m_program_rom.data()) + offset,
                      m_program_rom.size() - offset, false};
    }

    if (address >= kSamplesBase && address < kSamplesBase + kSamplesSize)
        return sample_window(address - kSamplesBase, kSamplesSize - (address - kSamplesBase));
    if (address >= kBank4Base && address < kBank4Base + kBank4Size)
        return sample_window(m_bank4_offset + (address - kBank4Base),
                             kBank4Size - (address - kBank4Base));
    if (address >= kBank5Base && address < kBank5Base + kBank5Size)
        return sample_window(m_bank5_offset + (address - kBank5Base),
                             kBank5Size - (address - kBank5Base));

    return {};
}

void SoundBoard::bus_wait(std::uint32_t address) {
    if (address < kRamBase + kRamSize ||
        (address >= kScspBase && address < kScspBase + kScspSize)) {
        const std::uint32_t contention = (kContentionCycles * m_scsp.active_slots() + 16) / 32;
        m_cpu.stall(static_cast<int>(kBusWaitCycles + contention));
    }
}

void SoundBoard::snd_ctrl_write(std::uint16_t value) {
    ++m_counters.snd_ctrl_writes;
    // Sample banking, and only for games whose sample region is larger than
    // 8 MB. DOA's 8 MB region is exactly at the threshold, so this does nothing
    // for it; MAME guards it the same way.
    if (m_samples.size() <= 0x800000)
        return;
    if ((value & 0x20) != 0) {
        m_bank4_offset = 0x200000;
        m_bank5_offset = 0x600000;
    } else {
        m_bank4_offset = 0x800000;
        m_bank5_offset = 0xa00000;
    }
}

// ---------------------------------------------------------------------------
// The 68000's bus
// ---------------------------------------------------------------------------
// read16/write16 are the primitives: the 68000's bus is 16 bits wide, so a
// longword access really is two word accesses, and building the wider ones out
// of them keeps accesses that straddle two regions correct for free.

std::uint16_t SoundBoard::read16(std::uint32_t address) {
    address &= kAddressMask;
    bus_wait(address);

    if (const Window window = resolve(address); window.base != nullptr) {
        if (address >= kSamplesBase)
            ++m_counters.sample_reads;
        if (window.size >= 2)
            return static_cast<std::uint16_t>((window.base[0] << 8) | window.base[1]);
        return static_cast<std::uint16_t>(window.base[0] << 8);
    }

    if (address >= kScspBase && address < kScspBase + kScspSize) {
        ++m_counters.scsp_reads;
        return m_scsp.read((address - kScspBase) >> 1);
    }

    if (address >= kSndCtrl && address < kSndCtrl + 2)
        return 0;  // write-only banking register, open bus

    ++m_counters.unmapped_reads;
    SM2_TRACE("sound: unmapped read16 at %06x", address);
    return 0;
}

void SoundBoard::write16(std::uint32_t address, std::uint16_t value) {
    address &= kAddressMask;
    bus_wait(address);

    if (const Window window = resolve(address); window.base != nullptr) {
        if (!window.writable)
            return;  // ROM, discarded as MAME's .rom() does
        window.base[0] = static_cast<std::uint8_t>(value >> 8);
        if (window.size >= 2)
            window.base[1] = static_cast<std::uint8_t>(value);
        return;
    }

    if (address >= kScspBase && address < kScspBase + kScspSize) {
        ++m_counters.scsp_writes;
        m_scsp.write((address - kScspBase) >> 1, value, 0xffff);
        return;
    }

    if (address >= kSndCtrl && address < kSndCtrl + 2) {
        snd_ctrl_write(value);
        return;
    }

    ++m_counters.unmapped_writes;
    SM2_TRACE("sound: unmapped write16 at %06x = %04x", address, value);
}

std::uint8_t SoundBoard::read8(std::uint32_t address) {
    // Byte lane 0 of the word is the high half on a big-endian bus.
    const std::uint16_t word = read16(address & ~1u);
    return (address & 1) != 0 ? static_cast<std::uint8_t>(word)
                              : static_cast<std::uint8_t>(word >> 8);
}

void SoundBoard::write8(std::uint32_t address, std::uint8_t value) {
    const std::uint32_t masked = address & kAddressMask;
    bus_wait(masked);

    // Plain memory takes the byte directly; anything with side effects has to be
    // told which lane was driven, because a byte write to a 16-bit register must
    // not clear the other half.
    if (const Window window = resolve(masked); window.base != nullptr) {
        if (window.writable)
            *window.base = value;
        return;
    }

    if (masked >= kScspBase && masked < kScspBase + kScspSize) {
        ++m_counters.scsp_writes;
        const std::uint16_t mask = (masked & 1) != 0 ? std::uint16_t{0x00ff}
                                                     : std::uint16_t{0xff00};
        const std::uint16_t wide = (masked & 1) != 0
                                       ? static_cast<std::uint16_t>(value)
                                       : static_cast<std::uint16_t>(value << 8);
        m_scsp.write((masked - kScspBase) >> 1, wide, mask);
        return;
    }

    if (masked >= kSndCtrl && masked < kSndCtrl + 2) {
        snd_ctrl_write(value);
        return;
    }

    ++m_counters.unmapped_writes;
    SM2_TRACE("sound: unmapped write8 at %06x = %02x", masked, value);
}

std::uint32_t SoundBoard::read32(std::uint32_t address) {
    const std::uint32_t high = read16(address);
    const std::uint32_t low = read16(address + 2);
    return (high << 16) | low;
}

void SoundBoard::write32(std::uint32_t address, std::uint32_t value) {
    write16(address, static_cast<std::uint16_t>(value >> 16));
    write16(address + 2, static_cast<std::uint16_t>(value));
}

// ---------------------------------------------------------------------------
// The SCSP's bus
// ---------------------------------------------------------------------------
// The SCSP has a 20-bit big-endian space of its own, with the same 512 KB RAM
// mapped into it, which is how a slot's sample address reaches data the 68000
// loaded. Addresses above RAM read from the sample ROM, which is where the SCSP
// finds PCM data the 68000 did not copy into RAM: RAM at 0x00000-0x7FFFF and the
// first 512 KB of sample ROM at 0x80000-0xFFFFF.

std::uint8_t SoundBoard::scsp_read_byte(std::uint32_t address) {
    address &= kScspAddressMask;
    if (address < m_ram.size())
        return m_ram[address];
    const std::uint32_t rom_offset = address - static_cast<std::uint32_t>(m_ram.size());
    if (rom_offset < m_samples.size())
        return m_samples[rom_offset];
    return 0;
}

std::uint16_t SoundBoard::scsp_read_word(std::uint32_t address) {
    address &= kScspAddressMask & ~1u;
    if (address + 1 < m_ram.size())
        return static_cast<std::uint16_t>((m_ram[address] << 8) | m_ram[address + 1]);
    const std::uint32_t rom_offset = address - static_cast<std::uint32_t>(m_ram.size());
    if (rom_offset + 1 < m_samples.size())
        return static_cast<std::uint16_t>((m_samples[rom_offset] << 8) |
                                          m_samples[rom_offset + 1]);
    return 0;
}

void SoundBoard::scsp_write_word(std::uint32_t address, std::uint16_t value) {
    address &= kScspAddressMask & ~1u;
    if (address + 1 >= m_ram.size())
        return;  // writes above RAM are discarded (ROM is read-only)
    m_ram[address] = static_cast<std::uint8_t>(value >> 8);
    m_ram[address + 1] = static_cast<std::uint8_t>(value);
}

}  // namespace sage::hw
