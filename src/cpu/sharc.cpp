// Standalone Analog Devices ADSP-21062 "SHARC" interpreter.
//
// Ported from MAME's src/devices/cpu/sharc (Ville Linde, BSD-3-Clause) with the
// device/execute/memory frameworks removed. The opcode bodies are kept verbatim
// in sharc_ops.inc / sharc_compute.inc (generated from the MAME sources) and are
// compiled as member functions of sage::cpu::Sharc.
//
// Internal SRAM model: 0x8000 48-bit words at address 0x20000..0x27fff. The PM
// and DM (32-bit) views index the same words; the 16-bit DM view at
// 0x40000..0x4ffff addresses three halfwords per word, little-endian.

#include "sage/cpu/sharc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace sage::cpu {

namespace {
// 16-bit little-endian halfword view of the 48-bit internal SRAM.
inline std::uint16_t sram_half(const std::uint64_t* sram, std::uint32_t index) {
    const std::uint64_t w = sram[index / 3];
    return std::uint16_t((w >> (16 * (index % 3))) & 0xffff);
}
inline void sram_half_set(std::uint64_t* sram, std::uint32_t index, std::uint16_t v) {
    const unsigned sh = 16 * (index % 3);
    std::uint64_t& w = sram[index / 3];
    w = (w & ~(std::uint64_t(0xffff) << sh)) | (std::uint64_t(v) << sh);
}
}  // namespace

Sharc::Sharc() { m_core = new sharc_internal_state(); }

void Sharc::reset() {
    std::memset(m_core, 0, sizeof(sharc_internal_state));
    std::memset(m_block, 0, sizeof(m_block));

    switch (m_boot_mode) {
        case BootMode::Eprom:
            m_core->dma[6].int_index = 0x20000;
            m_core->dma[6].int_modifier = 1;
            m_core->dma[6].int_count = 0x100;
            m_core->dma[6].ext_index = 0x400000;
            m_core->dma[6].ext_modifier = 1;
            m_core->dma[6].ext_count = 0x600;
            m_core->dma[6].control = 0x2a1;
            sharc_dma_exec(6);
            dma_op(6);
            m_core->dma_op[6].timer = 0;
            break;

        case BootMode::Host:
            // The host streams the program in through the external port; only
            // the channel-6 index/count registers are primed here.
            m_core->dma[6].int_index = 0x20000;
            m_core->dma[6].int_modifier = 1;
            m_core->dma[6].int_count = 0x100;
            m_core->dma[6].ext_index = 0x400000;
            m_core->dma[6].ext_modifier = 1;
            m_core->dma[6].ext_count = 0x600;
            m_core->dma[6].control = 0xa1;
            break;

        default:
            throw SharcFault("SHARC: Unimplemented boot mode");
    }

    m_core->pc = 0x20004;
    m_core->extdma_shift = 0;
    m_core->daddr = m_core->pc + 1;
    m_core->faddr = m_core->daddr + 1;
    m_core->nfaddr = m_core->faddr + 1;

    m_core->idle = 0;
    m_core->mode1 = 0x00000000;
    m_core->mode2 &= 0xf0000000;
    m_core->astat &= FLG0 | FLG1 | FLG2 | FLG3;
    m_core->stky = PCEM | SSEM | LSEM;
    m_core->irptl = 0x0000;
    m_core->imask = 0x0003;
    m_core->ustat1 = 0x0000;
    m_core->ustat2 = 0x0000;

    m_core->pcstkp = 0;
    m_core->lstkp = 0;
    m_core->pcstk = 0x00ffffff;
    m_core->curlcntr = 0xffffffff;
    m_core->lcntr = m_core->lcstack[0];
    m_core->laddr.unpack(0xffffffff);
    m_core->status_stkp = 0;
    m_core->interrupt_active = 0;

    m_core->syscon = 0x00000010;
    m_core->sysstat &= 0x00000ff0;
    m_core->iop_write_num = 0;
    m_core->iop_data = 0;

    m_sse = false;
    m_instructions = 0;
    build_opcode_table();
}

// ---- Memory accessors ------------------------------------------------------
// Internal SRAM is stored as MAME's `m_blocks` two-array packing. A 48-bit PM
// word is split across three cells (MSW, LSW-high16, LSW-low16) selected by the
// slot parity; the DM 32-bit and 16-bit apertures address the same cells
// directly. Formulas and map offsets mirror MAME's pgm_2m/data_2m maps exactly.
namespace {
constexpr std::uint32_t kUnmapped = 0xffffffffu;
constexpr std::uint64_t kUnmapped48 = 0x0000ffffffffffffull;
}

bool Sharc::pm_route(std::uint32_t word, int& block, std::uint32_t& moff) {
    const std::uint32_t off = (word - 0x20000u) & 0x3ffffu;
    if (off <= 0x4fff) {
        block = 0;
        moff = off;
        return true;
    }
    // Block 1 repeats across the 0x8000/0x10000 address bits (MAME pgm_2m
    // mirror(0x18000)), so it answers at 0x28000, 0x30000 and 0x38000.
    if (off >= 0x8000 && off <= 0x1cfff && (off & 0x7fff) <= 0x4fff) {
        block = 1;
        moff = off & 0x7fff;
        return true;
    }
    return false;
}

bool Sharc::dm_route(std::uint32_t word, int& block, std::uint32_t& cell) {
    const std::uint32_t a = word & 0x7ffffu;
    if (a >= 0x20000 && a <= 0x27fff) {  // block 0, normal-word view
        block = 0;
        cell = a - 0x20000;
        return true;
    }
    if (a >= 0x28000 && a <= 0x3ffff) {  // block 1 normal-word view (mirror(0x18000))
        block = 1;
        cell = a & 0x7fff;
        return true;
    }
    if (a >= 0x40000 && a <= 0x4ffff) {  // block 0, halfword view
        block = 0;
        cell = (a - 0x40000) / 2;
        return true;
    }
    if (a >= 0x50000 && a <= 0x7ffff) {  // block 1 halfword view (mirrored range)
        block = 1;
        cell = (a & 0xffff) / 2;
        return true;
    }
    return false;
}

std::uint32_t Sharc::dm_short_r(std::uint32_t off, int block, bool sign_extend) const {
    const std::uint32_t cell = m_block[block][off >> 1];
    const std::uint32_t hw = (off & 1) ? (cell >> 16) : (cell & 0xffff);
    return sign_extend ? std::uint32_t(sext(hw, 16)) : hw;
}

void Sharc::dm_short_w(std::uint32_t off, int block, std::uint32_t data) {
    std::uint32_t& cell = m_block[block][off >> 1];
    if (off & 1)
        cell = (cell & 0x0000ffff) | (data << 16);
    else
        cell = (cell & 0xffff0000) | (data & 0xffff);
}

std::uint64_t Sharc::pm_read48(std::uint32_t word) const {
    int block;
    std::uint32_t moff;
    if (!pm_route(word, block, moff)) return kUnmapped48;
    const std::uint32_t slot = moff >> 12;
    const std::uint32_t base = (moff & 0xfff) + (slot >> 1) * 0x3000;
    if (slot & 1)
        return (std::uint64_t(m_block[block][base + 0x2000]) << 16) |
               (m_block[block][base + 0x1000] & 0xffff);
    return (std::uint64_t(m_block[block][base]) << 16) |
           (m_block[block][base + 0x1000] >> 16);
}

void Sharc::pm_write48(std::uint32_t word, std::uint64_t data) {
    int block;
    std::uint32_t moff;
    if (!pm_route(word, block, moff)) return;
    const std::uint32_t slot = moff >> 12;
    const std::uint32_t base = (moff & 0xfff) + (slot >> 1) * 0x3000;
    if (slot & 1) {
        m_block[block][base + 0x1000] =
            (m_block[block][base + 0x1000] & 0xffff0000) | std::uint32_t(data & 0xffff);
        m_block[block][base + 0x2000] = std::uint32_t(data >> 16);
    } else {
        m_block[block][base] = std::uint32_t(data >> 16);
        m_block[block][base + 0x1000] =
            (m_block[block][base + 0x1000] & 0xffff) | (std::uint32_t(data & 0xffff) << 16);
    }
}

std::uint32_t Sharc::pm_read32(std::uint32_t word) const {
    return std::uint32_t(pm_read48(word) >> 16);
}

void Sharc::pm_write32(std::uint32_t word, std::uint32_t data) {
    pm_write48(word, std::uint64_t(data) << 16);
}

std::uint32_t Sharc::dm_read32(std::uint32_t word) const {
    if (word >= 0x400000u && m_ext_read) {
        std::uint32_t v = kUnmapped;
        if (m_ext_read(word, v)) return v;
        if (m_unmapped_dbg) std::fprintf(stderr, "[unmap-ext] pc=%06X dm=%08X\n", m_core->pc, word);
        return kUnmapped;
    }
    const std::uint32_t a = word & 0x7ffffu;
    if (a < 0x100) return iop_r(a);
    int block;
    std::uint32_t cell;
    if (!dm_route(a, block, cell)) {
        if (m_unmapped_dbg) std::fprintf(stderr, "[unmap-r] pc=%06X dm=%08X\n", m_core->pc, word);
        return kUnmapped;
    }
    // The 0x40000+ aperture is a 16-bit-wide view: a 32-bit access yields the
    // halfword at that offset, sign-extended when MODE1_SSE is selected.
    if (a >= 0x40000) {
        const int b = (a < 0x50000) ? 0 : 1;
        return dm_short_r(a - (b ? 0x50000 : 0x40000), b, m_sse);
    }
    return m_block[block][cell];
}

void Sharc::dm_write32(std::uint32_t word, std::uint32_t data) {
    if (word >= 0x400000u) {
        if (m_ext_write) m_ext_write(word, data);
        return;
    }
    const std::uint32_t a = word & 0x7ffffu;
    if (a < 0x100) {
        iop_w(a, data);
        return;
    }
    int block;
    std::uint32_t cell;
    if (!dm_route(a, block, cell)) return;
    if (a >= 0x40000) {
        const int b = (a < 0x50000) ? 0 : 1;
        dm_short_w(a - (b ? 0x50000 : 0x40000), b, data & 0xffff);
        return;
    }
    if (m_dm_dbg && (a >= 0x28000 || (a >= 0x20000 && a <= 0x20100)))
        std::fprintf(stderr, "[dmw] pc=%06X a=%06X blk=%d cell=%04X v=%08X\n", m_core->pc, a, block, cell, data);
    m_block[block][cell] = data;
}

std::uint32_t Sharc::debug_pm32(std::uint32_t word) const {
    return std::uint32_t(pm_read48(word) >> 16);
}
std::uint64_t Sharc::debug_pm48(std::uint32_t word) const { return pm_read48(word); }
std::uint32_t Sharc::debug_dm32(std::uint32_t word) const { return dm_read32(word); }
void Sharc::debug_set_pm48(std::uint32_t word, std::uint64_t data) { pm_write48(word, data); }
void Sharc::debug_set_dm32(std::uint32_t word, std::uint32_t data) { dm_write32(word, data); }

// ---- IOP registers ---------------------------------------------------------

std::uint32_t Sharc::iop_r(std::uint32_t offset) const {
    switch (offset) {
        case 0x00: return 0;  // System configuration
        case 0x37: return m_core->dma_status;
        default: return 0;
    }
}

void Sharc::iop_w(std::uint32_t offset, std::uint32_t data) {
    switch (offset) {
        case 0x00: m_core->syscon = data; break;
        case 0x02: break;  // External memory wait states
        case 0x04:         // External port DMA buffer 0
            external_dma_write(m_core->extdma_shift, data);
            if (++m_core->extdma_shift == 3) m_core->extdma_shift = 0;
            break;
        case 0x08: case 0x09: case 0x0a: case 0x0b:
        case 0x0c: case 0x0d: case 0x0e: case 0x0f:  // Message registers
            break;
        case 0x14: case 0x17: break;
        case 0x1c:  // DMA channel 6 control
            m_core->dma[6].control = data;
            if (data & 0x1) sharc_iop_delayed_write(0x1c, data, 1);
            break;
        case 0x20: break;
        case 0x40: m_core->dma[6].int_index = data; break;
        case 0x41: m_core->dma[6].int_modifier = data; break;
        case 0x42: m_core->dma[6].int_count = data; break;
        case 0x43: m_core->dma[6].chain_ptr = data; break;
        case 0x44: m_core->dma[6].gen_purpose = data; break;
        case 0x45: m_core->dma[6].ext_index = data; break;
        case 0x46: m_core->dma[6].ext_modifier = data; break;
        case 0x47: m_core->dma[6].ext_count = data; break;
        case 0x1d:  // DMA channel 7 control
            m_core->dma[7].control = data;
            if (data & 0x1) sharc_iop_delayed_write(0x1d, data, 30);
            break;
        case 0x48: m_core->dma[7].int_index = data; break;
        case 0x49: m_core->dma[7].int_modifier = data; break;
        case 0x4a: m_core->dma[7].int_count = data; break;
        case 0x4b: m_core->dma[7].chain_ptr = data; break;
        case 0x4c: m_core->dma[7].gen_purpose = data; break;
        case 0x4d: m_core->dma[7].ext_index = data; break;
        case 0x4e: m_core->dma[7].ext_modifier = data; break;
        case 0x4f: m_core->dma[7].ext_count = data; break;
        default:
            throw SharcFault("sharc_iop_w: Unimplemented IOP reg %02X, %08X", offset, data);
    }
}

void Sharc::sharc_iop_delayed_write(int reg, std::uint32_t data, int cycles) {
    m_core->iop_delayed_reg = std::uint32_t(reg);
    m_core->iop_delayed_data = data;
    m_delayed_iop_timer = cycles;
}

void Sharc::external_iop_write(std::uint32_t address, std::uint32_t data) {
    // Host packing mode determines the external host bus width (16/32).
    if ((m_core->syscon & 0x10) && address != 0x04) {
        if ((m_core->iop_write_num++ & 1) == 0) {
            m_core->iop_data = data & 0xffff;
            return;
        }
        m_core->iop_data |= (data & 0xffff) << 16;
    } else {
        m_core->iop_data = data;
    }

    if (address == 0x1c)
        m_core->dma[6].control = m_core->iop_data;
    else
        dm_write32(address, m_core->iop_data);
}

void Sharc::external_dma_write(std::uint32_t address, std::uint64_t data) {
    // Index registers are offset by 0x0002 0000, the first internal RAM word.
    const std::uint32_t index = (m_core->dma[6].int_index & 0x1ffff) | 0x20000;
    const unsigned mswf = BIT(m_core->dma[6].control, 8);
    const unsigned pmode = BIT(m_core->dma[6].control, 6, 2);
    const unsigned dtype = BIT(m_core->dma[6].control, 5);

    switch (pmode) {
        case 0:  // no packing
            if (dtype)
                pm_write32(index, std::uint32_t(data));
            else
                dm_write32(index, std::uint32_t(data));
            m_core->dma[6].int_index += m_core->dma[6].int_modifier;
            break;

        case 2: {  // 16/48 packing
            const unsigned word = address % 3;
            const unsigned shift = (mswf ? (2 - word) : word) * 16;
            std::uint64_t r = pm_read48(index);
            r &= ~(std::uint64_t(0xffff) << shift);
            r |= (data & 0xffff) << shift;
            pm_write48(index, r);
            if (word == 2) m_core->dma[6].int_index += m_core->dma[6].int_modifier;
            break;
        }

        default:
            throw SharcFault("sharc_external_dma_write: unimplemented packing mode %d", pmode);
    }
}

// ---- PC / flow -------------------------------------------------------------

void Sharc::CHANGE_PC(std::uint32_t newpc) {
    if (m_badpc_trace && newpc > 0x3ffffu)
        std::fprintf(stderr, "[badpc] CHANGE_PC from pc=%06X op=%012llX -> %08X\n",
                     m_core->pc, (unsigned long long)m_core->opcode, newpc);
    m_core->pc = newpc;
    m_core->daddr = newpc;
    m_core->faddr = newpc + 1;
    m_core->nfaddr = newpc + 2;
}

void Sharc::CHANGE_PC_DELAYED(std::uint32_t newpc) {
    if (m_badpc_trace && (newpc > 0x3ffffu || newpc < 0x20000u))
        std::fprintf(stderr, "[badpc] CHANGE_PC_DELAYED from pc=%06X op=%012llX -> %08X\n",
                     m_core->pc, (unsigned long long)m_core->opcode, newpc);
    m_core->nfaddr = newpc;
    m_core->delay_slot1 = m_core->pc;
    m_core->delay_slot2 = m_core->daddr;
}

void Sharc::set_flag_input(int flag_num, int state) {
    m_core->flag[flag_num] = state ? 1 : 0;
}

void Sharc::write_stall(int state) { m_core->write_stalled = (state != 0); }

void Sharc::update_inputs() {}

void Sharc::check_interrupts() {
    if ((m_core->imask & m_core->irq_pending) && (m_core->mode1 & MODE1_IRPTEN) &&
        !m_core->interrupt_active && m_core->pc != m_core->delay_slot1 &&
        m_core->pc != m_core->delay_slot2) {
        int which = 0;
        for (int i = 0; i < 32; i++) {
            if (BIT(m_core->irq_pending, i)) break;
            which++;
        }

        PUSH_PC();
        if (m_core->idle)
            m_core->pcstk = m_core->pc + 1;
        else
            m_core->pcstk = m_core->daddr;

        m_core->irptl |= 1u << which;

        if (which >= 6 && which <= 8) PUSH_STATUS_STACK();

        CHANGE_PC(0x20000 + (which * 0x4));

        m_core->active_irq_num = which;
        m_core->irq_pending &= ~(1 << which);
        m_core->interrupt_active = 1;
    }
}

// ---- Execution -------------------------------------------------------------

int Sharc::step() {
    m_core->icount = 1;
    run_cycles(1);
    return 1;
}

void Sharc::run_cycles(int cycles) {
    m_core->icount = cycles;

    if (m_core->write_stalled) m_core->icount = 0;

    if (m_core->idle && m_core->irq_pending == 0) {
        int dma_count = m_core->icount;
        while ((dma_count > 0) && (m_core->dma_status & ((1 << 6) | (1 << 7)))) {
            if (!m_core->write_stalled) {
                dma_run_cycle(6);
                dma_run_cycle(7);
            }
            dma_count--;
        }
        m_core->icount = 0;
    }
    if (m_core->irq_pending != 0) {
        check_interrupts();
        m_core->idle = 0;
    }

    while (m_core->icount > 0 && !m_core->idle && !m_core->write_stalled) {
        m_core->pc = m_core->daddr;
        m_core->daddr = m_core->faddr;
        m_core->faddr = m_core->nfaddr;
        m_core->nfaddr++;

        m_core->astat_old_old_old = m_core->astat_old_old;
        m_core->astat_old_old = m_core->astat_old;
        m_core->astat_old = m_core->astat;

        m_core->opcode = pm_read48(m_core->pc);

        if (m_ring) {
            m_ring[m_ring_pos & 1023] = m_core->pc;
            m_ring_op[m_ring_pos & 1023] = m_core->opcode;
            m_ring_d[m_ring_pos & 1023] = m_core->daddr;
            m_ring_f[m_ring_pos & 1023] = m_core->faddr;
            m_ring_nf[m_ring_pos & 1023] = m_core->nfaddr;
            ++m_ring_pos;
            if ((m_core->pc > 0x3ffffu || m_core->pc < 0x20000u) && !m_ring_dumped) {
                m_ring_dumped = true;
                std::fprintf(stderr, "[ring] divergence at pc=%06X, last 200 fetches:\n", m_core->pc);
                for (unsigned k = 824; k < 1024; ++k) {
                    unsigned idx = (m_ring_pos - 1024 + k) & 1023;
                    std::fprintf(stderr, "  [ring] %06X op=%012llX d=%06X f=%06X nf=%06X\n", m_ring[idx],
                                 (unsigned long long)m_ring_op[idx], m_ring_d[idx], m_ring_f[idx],
                                 m_ring_nf[idx]);
                }
            }
        }

        if (m_cmd_trig && m_cmd_started && m_core->pc >= 0x20124u && m_core->pc <= 0x20132u)
            std::fprintf(stderr, "[reg] pc=%06X op=%012llX r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X i2=%06X m7=%08X d2i7=%06X\n",
                         m_core->pc, (unsigned long long)m_core->opcode, m_core->r[0].r, m_core->r[1].r,
                         m_core->r[2].r, m_core->r[3].r, m_core->r[4].r, m_core->r[5].r,
                         m_core->dag1.i[2], m_core->dag1.m[7], m_core->dag2.i[7]);
        if (m_cmd_trig && m_core->pc == 0x20132u) {
            const std::uint32_t tgt = m_core->dag2.i[7] + m_core->dag2.m[0];
            std::fprintf(stderr, "[dispatch] d2i7=%06X d2m0=%06X target=%08X r0=%08X%s\n",
                         m_core->dag2.i[7], m_core->dag2.m[0], tgt, m_core->r[0].r,
                         (tgt < 0x20000u || tgt > 0x3ffffu) ? "  <-- INVALID" : "");
        }
        if (m_cmd_trig && !m_cmd_started && m_core->pc == 0x20117u && m_core->flag[0] == 0) {
            m_cmd_started = true;
            m_cmd_left = 120;
            std::fprintf(stderr, "[cmd] === first command: flag0 cleared at pc=020117 ===\n");
        }
        if (m_cmd_trig && m_cmd_started && m_cmd_left > 0 && m_core->pc != 0x20117u) {
            --m_cmd_left;
            std::fprintf(stderr, "[cmd] pc=%06X op=%012llX d1i=%08X d1m=%08X d2i=%08X d2m=%08X r3=%08X\n",
                         m_core->pc, (unsigned long long)m_core->opcode, m_core->dag1.i[2],
                         m_core->dag1.m[7], m_core->dag2.i[7], m_core->dag2.m[0], m_core->r[3].r);
        }
        if (m_win_lo && m_core->pc >= m_win_lo && m_core->pc <= m_win_hi && m_core->pc != 0x20123u) {
            std::fprintf(stderr,
                         "[win] pc=%06X op=%012llX d=%06X f=%06X nf=%06X pcstk=%06X pcstkp=%u "
                         "flg=%u%u\n",
                         m_core->pc, (unsigned long long)m_core->opcode, m_core->daddr, m_core->faddr,
                         m_core->nfaddr, m_core->pcstk, m_core->pcstkp, m_core->flag[0],
                         m_core->flag[1]);
            std::fprintf(stderr, "[win]   d1i=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                         m_core->dag1.i[0], m_core->dag1.i[1], m_core->dag1.i[2], m_core->dag1.i[3],
                         m_core->dag1.i[4], m_core->dag1.i[5], m_core->dag1.i[6], m_core->dag1.i[7]);
            std::fprintf(stderr, "[win]   d1m=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                         m_core->dag1.m[0], m_core->dag1.m[1], m_core->dag1.m[2], m_core->dag1.m[3],
                         m_core->dag1.m[4], m_core->dag1.m[5], m_core->dag1.m[6], m_core->dag1.m[7]);
            std::fprintf(stderr, "[win]   d2i=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                         m_core->dag2.i[0], m_core->dag2.i[1], m_core->dag2.i[2], m_core->dag2.i[3],
                         m_core->dag2.i[4], m_core->dag2.i[5], m_core->dag2.i[6], m_core->dag2.i[7]);
            std::fprintf(stderr, "[win]   d2m=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                         m_core->dag2.m[0], m_core->dag2.m[1], m_core->dag2.m[2], m_core->dag2.m[3],
                         m_core->dag2.m[4], m_core->dag2.m[5], m_core->dag2.m[6], m_core->dag2.m[7]);
        }

        if (m_trace_count > 0) {
            --m_trace_count;
            std::fprintf(stderr,
                         "[sh] pc=%06X op=%012llX idle=%d irqp=%X imask=%X mode1=%X flg=%u%u%u%u "
                         "lcntr=%X lstkp=%u pcstkp=%u laddr=%06X lt=%u\n",
                         m_core->pc, (unsigned long long)m_core->opcode, m_core->idle,
                         m_core->irq_pending, m_core->imask, m_core->mode1, m_core->flag[0],
                         m_core->flag[1], m_core->flag[2], m_core->flag[3], m_core->lcntr,
                         m_core->lstkp, m_core->pcstkp, m_core->laddr.addr, m_core->laddr.loop_type);
        }

        // Looping
        if (!(m_core->stky & LSEM) && (m_core->pc == m_core->laddr.addr)) {
            if (m_loop_dbg && (m_core->pc >= 0x20120u && m_core->pc <= 0x20140u))
                std::fprintf(stderr,
                             "[loopfire] pc=%06X type=%u code=%02X cond=%d top=%06X lstkp=%u\n",
                             m_core->pc, m_core->laddr.loop_type, m_core->laddr.code,
                             DO_CONDITION_CODE(m_core->laddr.code), TOP_PC(), m_core->lstkp);
            switch (m_core->laddr.loop_type) {
                case 0:  // arithmetic condition-based
                    if ((m_core->pc - TOP_PC()) > 2) m_core->astat = m_core->astat_old_old_old;
                    if (m_wait_dbg && m_core->pc == 0x20123u && (m_wait_dbg < 0 || true)) {
                        std::fprintf(stderr, "[loop] pc=%06X code=%X cond=%d flag0=%u lc=%X lstkp=%u\n",
                                     m_core->pc, m_core->laddr.code, DO_CONDITION_CODE(m_core->laddr.code),
                                     m_core->flag[0], m_core->curlcntr, m_core->lstkp);
                        if (m_wait_dbg > 0) --m_wait_dbg;
                    }
                    if (DO_CONDITION_CODE(m_core->laddr.code)) {
                        POP_LOOP();
                        POP_PC();
                    } else {
                        CHANGE_PC(TOP_PC());
                    }
                    m_core->astat = m_core->astat_old;
                    break;
                case 1:  // counter-based, length 1
                case 2:  // counter-based, length 2
                case 3:  // counter-based, length >2
                    --m_core->curlcntr;
                    if (m_core->curlcntr == 0) {
                        POP_LOOP();
                        POP_PC();
                    } else {
                        CHANGE_PC(TOP_PC());
                    }
                    break;
            }
        }

        if (m_pc_hist) ++(*m_pc_hist)[m_core->pc];
        (this->*m_sharc_op[(m_core->opcode >> 39) & 0x1ff])();
        m_instructions++;

        // System register latency effect
        if (m_core->systemreg_latency_cycles > 0) {
            --m_core->systemreg_latency_cycles;
            if (m_core->systemreg_latency_cycles <= 0) systemreg_write_latency_effect();
        }

        if (m_delayed_iop_timer > 0) {
            --m_delayed_iop_timer;
            if (m_delayed_iop_timer <= 0) {
                if (m_core->iop_delayed_reg == 0x1c) {
                    if (m_core->iop_delayed_data & 0x1) sharc_dma_exec(6);
                } else if (m_core->iop_delayed_reg == 0x1d) {
                    if (m_core->iop_delayed_data & 0x1) sharc_dma_exec(7);
                }
            }
        }

        // DMA cycle-stealing
        for (int ch = 0; ch < 12; ch++) {
            if (m_core->dma_op[ch].active && m_core->dma_op[ch].timer > 0) {
                if (--m_core->dma_op[ch].timer <= 0) {
                    m_core->irptl |= 1u << (ch + 10);
                    if (m_core->imask & (1u << (ch + 10))) m_core->irq_pending |= 1 << (ch + 10);
                    dma_op(ch);
                }
            }
        }

        if (!m_core->write_stalled) {
            dma_run_cycle(6);
            dma_run_cycle(7);
        }

        --m_core->icount;
    }
}

// ---- Opcode dispatch table -------------------------------------------------

void Sharc::build_opcode_table() {
    for (int i = 0; i < 0x200; i++) {
        m_sharc_op[i] = &Sharc::op_unimplemented;
        const std::uint16_t op = std::uint16_t(i << 7);
        for (std::size_t j = 0; j < s_num_ops; j++) {
            const auto& opcode = s_sharc_opcode_table[j];
            if ((opcode.op_mask & op) == opcode.op_bits) {
                m_sharc_op[i] = opcode.handler;
                break;
            }
        }
    }
}

void Sharc::op_unimplemented() {
    throw SharcFault("%s: Unimplemented opcode %012llX at %08X", tag(),
                     (unsigned long long)m_core->opcode, m_core->pc);
}

std::string Sharc::disassemble(std::uint32_t pc, std::uint64_t opcode) const {
    (void)pc;
    return sharc_format("%012llX", (unsigned long long)opcode);
}

// ---- Opcode bodies ---------------------------------------------------------

#include "sharc_dma.inc"
#include "sharc_ops.inc"
#include "sharc_opcode_table.inc"

}  // namespace sage::cpu
