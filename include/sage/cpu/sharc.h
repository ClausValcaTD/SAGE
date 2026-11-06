#pragma once

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace sage::cpu {

using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;

// Analog Devices ADSP-21062 "SHARC" interpreter, decoupled from MAME's
// src/devices/cpu/sharc (Ville Linde, BSD-3-Clause) into a standalone C++20
// class. The MAME device/execute/memory frameworks are replaced by direct
// array accesses; only the DRC/JIT path is omitted (the interpreter is used).
//
// Memory model (ADSP-21062, 2 Mbit internal SRAM):
//   PM (program)  : 24-bit address, 48-bit words
//   DM (data)     : 32-bit address, 32-bit words
//   Internal SRAM : two 0x4000-word blocks, 48-bit wide, shared by PM and DM.
//                   PM is addressed 0x20000..0x27fff, DM 0x20000..0x27fff
//                   (32-bit view) and 0x40000..0x4ffff (16-bit view).
//
// The host (i960) talks to the SHARC through the external port:
// external_iop_write() for the IOP registers and external_dma_write() for the
// DMA-fed program upload. FLAG0/FLAG1 are inputs wired to the FIFO state.

// ---- Small MAME compatibility shims used by the ported opcode code ---------

inline std::string sharc_format(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

// Raised for architectural conditions the core does not implement, mirroring
// MAME's fatalerror()/emu_fatalerror call sites so a bad decode is recoverable.
class SharcFault : public std::runtime_error {
public:
    template <typename... Args>
    explicit SharcFault(const char* fmt, Args... args)
        : std::runtime_error(sharc_format(fmt, args...)) {}
};

template <typename... Args>
[[noreturn]] inline void fatalerror(const char* fmt, Args... args) {
    throw SharcFault(fmt, args...);
}

inline const char* tag() { return "sharc"; }

template <typename T>
constexpr std::uint32_t BIT(T value, int start) {
    return (std::uint64_t(value) >> start) & 1u;
}
template <typename T>
constexpr std::uint32_t BIT(T value, int start, int count) {
    return (std::uint64_t(value) >> start) & ((std::uint64_t(1) << count) - 1u);
}

template <typename T>
constexpr std::int32_t sext(T value, int bits) {
    const std::uint64_t m = std::uint64_t(1) << (bits - 1);
    return std::int32_t((std::uint64_t(value) ^ m) - m);
}
template <typename T>
constexpr std::uint32_t make_bitmask(int bits) {
    return std::uint32_t((std::uint64_t(1) << bits) - 1u);
}

class Sharc {
public:
    enum class BootMode { Eprom, Host, Link, NoBoot };

    Sharc();

    // Clears internal SRAM and establishes the reset state. For BootMode::Host
    // the DMA channel 6 registers are primed so the host can stream the program
    // in through external_dma_write().
    void reset();
    void set_boot_mode(BootMode mode) { m_boot_mode = mode; }

    // Executes `cycles` instruction cycles.
    void run_cycles(int cycles);

    // Executes one instruction; returns the cycles consumed.
    int step();

    // ---- External (host) port ---------------------------------------------
    void external_iop_write(std::uint32_t address, std::uint32_t data);
    void external_dma_write(std::uint32_t address, std::uint64_t data);

    // FLAG0..3 inputs (mode2 must select them as inputs).
    void set_flag_input(int flag_num, int state);
    std::uint32_t flag_input(int n) const { return m_core->flag[n]; }
    void write_stall(int state);

    std::uint32_t pc() const { return m_core->pc; }
    std::uint32_t reg(int i) const { return std::uint32_t(m_core->r[i].r); }
    bool idle() const { return m_core->idle != 0; }
    std::uint64_t instructions() const { return m_instructions; }
    void set_pc_hist(std::map<std::uint32_t, std::uint64_t>* h) { m_pc_hist = h; }
    std::uint32_t irq_pending() const { return std::uint32_t(m_core->irq_pending); }
    std::uint32_t dma_status() const { return m_core->dma_status; }
    std::uint32_t mode1() const { return m_core->mode1; }
    std::uint32_t mode2() const { return m_core->mode2; }

    // Direct internal-memory inspection (bring-up aids), word-addressed.
    std::uint32_t debug_pm32(std::uint32_t word) const;
    std::uint64_t debug_pm48(std::uint32_t word) const;
    std::uint32_t debug_dm32(std::uint32_t word) const;
    void debug_set_pm48(std::uint32_t word, std::uint64_t data);
    void debug_set_dm32(std::uint32_t word, std::uint32_t data);

    std::string disassemble(std::uint32_t pc, std::uint64_t opcode) const;

    // ---- External DM port (board wiring) -----------------------------------
    // Data-space accesses at or above 0x400000 leave the chip and are handled by
    // the host (the Model 2B board maps the FIFOs, buffer RAM and copro_data ROM
    // there). Returning false from a read leaves the value unhandled (0xffffffff).
    using ExtReadFn = std::function<bool(std::uint32_t addr, std::uint32_t& data)>;
    using ExtWriteFn = std::function<void(std::uint32_t addr, std::uint32_t data)>;
    void set_external_port(ExtReadFn rd, ExtWriteFn wr) {
        m_ext_read = std::move(rd);
        m_ext_write = std::move(wr);
    }

private:
    // ---- ASTAT / STKY / MODE1 / MODE2 flag bits ----------------------------
    static constexpr std::uint32_t AZ = 0x0000'0001;
    static constexpr std::uint32_t AV = 0x0000'0002;
    static constexpr std::uint32_t AN = 0x0000'0004;
    static constexpr std::uint32_t AC = 0x0000'0008;
    static constexpr std::uint32_t AS = 0x0000'0010;
    static constexpr std::uint32_t AI = 0x0000'0020;
    static constexpr std::uint32_t MN = 0x0000'0040;
    static constexpr std::uint32_t MV = 0x0000'0080;
    static constexpr std::uint32_t MU = 0x0000'0100;
    static constexpr std::uint32_t MI = 0x0000'0200;
    static constexpr std::uint32_t AF = 0x0000'0400;
    static constexpr std::uint32_t SV = 0x0000'0800;
    static constexpr std::uint32_t SZ = 0x0000'1000;
    static constexpr std::uint32_t SS = 0x0000'2000;
    static constexpr std::uint32_t BTF = 0x0004'0000;
    static constexpr std::uint32_t FLG0 = 0x0008'0000;
    static constexpr std::uint32_t FLG1 = 0x0010'0000;
    static constexpr std::uint32_t FLG2 = 0x0020'0000;
    static constexpr std::uint32_t FLG3 = 0x0040'0000;

    static constexpr std::uint32_t AUS = 0x0000'0001;
    static constexpr std::uint32_t AVS = 0x0000'0002;
    static constexpr std::uint32_t AOS = 0x0000'0004;
    static constexpr std::uint32_t AIS = 0x0000'0020;
    static constexpr std::uint32_t MOS = 0x0000'0040;
    static constexpr std::uint32_t MVS = 0x0000'0080;
    static constexpr std::uint32_t MUS = 0x0000'0100;
    static constexpr std::uint32_t MIS = 0x0000'0200;
    static constexpr std::uint32_t PCFL = 0x0020'0000;
    static constexpr std::uint32_t PCEM = 0x0040'0000;
    static constexpr std::uint32_t SSOV = 0x0080'0000;
    static constexpr std::uint32_t SSEM = 0x0100'0000;
    static constexpr std::uint32_t LSOV = 0x0200'0000;
    static constexpr std::uint32_t LSEM = 0x0400'0000;

    static constexpr std::uint32_t MODE1_BR8 = 0x0000'0001;
    static constexpr std::uint32_t MODE1_BR0 = 0x0000'0002;
    static constexpr std::uint32_t MODE1_SRCU = 0x0000'0004;
    static constexpr std::uint32_t MODE1_SRD1H = 0x0000'0008;
    static constexpr std::uint32_t MODE1_SRD1L = 0x0000'0010;
    static constexpr std::uint32_t MODE1_SRD2H = 0x0000'0020;
    static constexpr std::uint32_t MODE1_SRD2L = 0x0000'0040;
    static constexpr std::uint32_t MODE1_SRRFH = 0x0000'0080;
    static constexpr std::uint32_t MODE1_SRRFL = 0x0000'0400;
    static constexpr std::uint32_t MODE1_NESTM = 0x0000'0800;
    static constexpr std::uint32_t MODE1_IRPTEN = 0x0000'1000;
    static constexpr std::uint32_t MODE1_ALUSAT = 0x0000'2000;
    static constexpr std::uint32_t MODE1_SSE = 0x0000'4000;
    static constexpr std::uint32_t MODE1_TRUNCATE = 0x0000'8000;
    static constexpr std::uint32_t MODE1_RND32 = 0x0001'0000;
    static constexpr std::uint32_t MODE1_CSEL = 0x0006'0000;

    static constexpr std::uint32_t MODE2_IRQ0E = 0x0000'0001;
    static constexpr std::uint32_t MODE2_IRQ1E = 0x0000'0002;
    static constexpr std::uint32_t MODE2_IRQ2E = 0x0000'0004;
    static constexpr std::uint32_t MODE2_CADIS = 0x0000'0010;
    static constexpr std::uint32_t MODE2_TIMEN = 0x0000'0020;
    static constexpr std::uint32_t MODE2_BUSLK = 0x0000'0040;
    static constexpr std::uint32_t MODE2_FLG0O = 0x0000'8000;
    static constexpr std::uint32_t MODE2_FLG1O = 0x0001'0000;
    static constexpr std::uint32_t MODE2_FLG2O = 0x0002'0000;
    static constexpr std::uint32_t MODE2_FLG3O = 0x0004'0000;
    static constexpr std::uint32_t MODE2_CAFRZ = 0x0008'0000;

    enum { FLG0_SHIFT = 19 };

    // FLAG0..3 output latches (MAME used devcb_write8 callbacks; nothing in
    // SAGE consumes them yet, so the written value is just recorded).
    std::uint32_t m_flag_out_value[4] = {0, 0, 0, 0};

    struct SHARC_REG {
        union {
            std::int32_t r;
            float f;
        };
    };

    struct SHARC_DAG {
        std::uint32_t i[8];
        std::uint32_t m[8];
        std::uint32_t b[8];
        std::uint32_t l[8];
    };

    struct SHARC_LADDR {
        std::uint32_t addr;
        std::uint32_t code;
        std::uint32_t loop_type;
        std::uint32_t pack() const { return (loop_type << 30) | (code << 24) | addr; }
        void unpack(std::uint32_t in) {
            addr = in & 0x00ff'ffff;
            code = (in >> 24) & 0x1f;
            loop_type = (in >> 30) & 3;
        }
    };

    struct SHARC_DMA_REGS {
        std::uint32_t control;
        std::uint32_t int_index;
        std::uint32_t int_modifier;
        std::uint32_t int_count;
        std::uint32_t chain_ptr;
        std::uint32_t gen_purpose;
        std::uint32_t ext_index;
        std::uint32_t ext_modifier;
        std::uint32_t ext_count;
    };

    struct SHARC_DMA_OP {
        std::uint32_t src;
        std::uint32_t dst;
        std::uint32_t chain_ptr;
        std::int32_t src_modifier;
        std::int32_t dst_modifier;
        std::int32_t src_count;
        std::int32_t dst_count;
        std::int32_t pmode;
        std::int32_t chained_direction;
        int timer;  // cycle countdown; <=0 means idle
        bool active;
        bool chained;
    };

    // MAME's sharc_internal_state, trimmed to the fields the interpreter uses.
    struct alignas(16) sharc_internal_state {
        SHARC_REG r[16];
        SHARC_REG reg_alt[16];

        std::uint32_t pc;
        std::uint64_t mrf;
        std::uint64_t mrb;

        std::uint32_t pcstack[30];
        std::uint32_t lcstack[6];
        std::uint32_t lastack[6];
        std::uint32_t lstkp;

        std::uint32_t faddr;
        std::uint32_t daddr;
        std::uint32_t pcstk;
        std::uint32_t pcstkp;
        SHARC_LADDR laddr;
        std::uint32_t curlcntr;
        std::uint32_t lcntr;
        std::uint8_t extdma_shift;
        std::uint32_t iop_write_num;
        std::uint32_t iop_data;

        SHARC_DAG dag1;
        SHARC_DAG dag2;
        SHARC_DAG dag1_alt;
        SHARC_DAG dag2_alt;

        SHARC_DMA_REGS dma[12];

        std::uint32_t mode1;
        std::uint32_t mode2;
        std::uint32_t astat;
        std::uint32_t stky;
        std::uint32_t irptl;
        std::uint32_t imask;
        std::uint32_t imaskp;
        std::uint32_t ustat1;
        std::uint32_t ustat2;

        std::uint32_t flag[4];

        std::uint32_t syscon;
        std::uint32_t sysstat;

        struct {
            std::uint32_t mode1;
            std::uint32_t astat;
        } status_stack[5];
        std::int32_t status_stkp;

        std::uint64_t px;

        int icount;
        std::uint64_t opcode;

        std::uint32_t nfaddr;

        std::int32_t idle;
        std::int32_t irq_pending;
        std::int32_t active_irq_num;

        SHARC_DMA_OP dma_op[12];
        std::uint32_t dma_status;
        bool write_stalled;

        std::int32_t interrupt_active;

        std::uint32_t iop_delayed_reg;
        std::uint32_t iop_delayed_data;

        std::uint32_t delay_slot1;
        std::uint32_t delay_slot2;

        std::int32_t systemreg_latency_cycles;
        std::int32_t systemreg_latency_reg;
        std::uint32_t systemreg_latency_data;
        std::uint32_t systemreg_previous_data;

        std::uint32_t astat_old;
        std::uint32_t astat_old_old;
        std::uint32_t astat_old_old_old;

        std::uint32_t arg0;
        std::uint32_t arg1;
        std::uint32_t arg2;
        std::uint32_t arg3;

        std::uint64_t arg64;
        std::uint32_t mode1_delay_data;

        std::uint32_t dreg_temp;
        std::uint32_t dreg_temp2;
        std::uint32_t jmpdest;
        std::uint32_t temp_return;

        struct {
            float k0_0;
            float k0_5;
            float k1_0;
            float k2_0;
        } fp_const;
    };

    sharc_internal_state* m_core = nullptr;
    BootMode m_boot_mode = BootMode::Host;
    // Cycle countdown for the delayed IOP write (MAME used an emu_timer).
    int m_delayed_iop_timer = 0;

    // Internal SRAM, replicated from MAME's `m_blocks` two-array packing. Each
    // block is a flat array of 32-bit cells that the PM (48-bit), DM (32-bit)
    // and DM (16-bit) views index with different formulas (see sharc.cpp).
    static constexpr std::uint32_t kBlockCells = 0x30000;
    std::uint32_t m_block[2][kBlockCells];

    // Replaces MAME's memory_view m_dm_short_view: selects the sign-extending
    // 16-bit DM read aperture when MODE1_SSE is set.
    bool m_sse = false;

    // 16-bit DM aperture (0x40000..0x7ffff); `off` is the halfword index.
    std::uint32_t dm_short_r(std::uint32_t off, int block, bool sign_extend) const;
    void dm_short_w(std::uint32_t off, int block, std::uint32_t data);
    // Resolves a PM word address to (block, map offset); false when unmapped.
    static bool pm_route(std::uint32_t word, int& block, std::uint32_t& moff);
    // Resolves a DM 32-bit address to (block, cell); false when unmapped.
    static bool dm_route(std::uint32_t word, int& block, std::uint32_t& cell);

    // The MAME DRC/JIT path is not ported; kept so sharcdma.hxx compiles as-is.
    static constexpr bool m_enable_drc = false;

    using OpFunc = void (Sharc::*)();

    static const struct SHARC_OP {
        std::uint32_t op_mask;
        std::uint32_t op_bits;
        OpFunc handler;
    } s_sharc_opcode_table[];
    static const std::size_t s_num_ops;
    OpFunc m_sharc_op[0x200];

    std::uint64_t m_instructions = 0;
    std::map<std::uint32_t, std::uint64_t>* m_pc_hist = nullptr;
public:
    long m_trace_count = 0;
    long m_wait_dbg = 0;
    std::uint32_t* m_ring = nullptr;
    std::uint64_t* m_ring_op = nullptr;
    std::uint32_t* m_ring_d = nullptr;
    std::uint32_t* m_ring_f = nullptr;
    std::uint32_t* m_ring_nf = nullptr;
    unsigned m_ring_pos = 0;
    bool m_ring_dumped = false;
    std::uint32_t m_win_lo = 0, m_win_hi = 0;
    void set_window(std::uint32_t lo, std::uint32_t hi) { m_win_lo = lo; m_win_hi = hi; }
    bool m_badpc_trace = false;
    bool m_loop_dbg = false;
    bool m_cmd_trig = false;
    bool m_cmd_started = false;
    int m_cmd_left = 0;
    bool m_dm_dbg = false;
    bool m_unmapped_dbg = false;
    void set_ring(std::uint32_t* pc, std::uint64_t* op, std::uint32_t* d, std::uint32_t* f,
                  std::uint32_t* nf) {
        m_ring = pc;
        m_ring_op = op;
        m_ring_d = d;
        m_ring_f = f;
        m_ring_nf = nf;
    }
private:

    ExtReadFn m_ext_read;
    ExtWriteFn m_ext_write;

    // Reciprocal / reciprocal-square-root mantissa tables (MAME compute.hxx).
    static const std::uint32_t recips_mantissa_lookup[128];
    static const std::uint32_t rsqrts_mantissa_lookup[128];

    // ---- Memory accessors (MAME sharcinternal.ipp) -------------------------
    // Word-addressed, matching the SHARC's native PM/DM addressing.
    std::uint32_t pm_read32(std::uint32_t word) const;
    void pm_write32(std::uint32_t word, std::uint32_t data);
    std::uint64_t pm_read48(std::uint32_t word) const;
    void pm_write48(std::uint32_t word, std::uint64_t data);
    std::uint32_t dm_read32(std::uint32_t word) const;
    void dm_write32(std::uint32_t word, std::uint32_t data);

    std::uint32_t iop_r(std::uint32_t offset) const;
    void iop_w(std::uint32_t offset, std::uint32_t data);

    void CHANGE_PC(std::uint32_t newpc);
    void CHANGE_PC_DELAYED(std::uint32_t newpc);

    void check_interrupts();
    void sharc_iop_delayed_write(int reg, std::uint32_t data, int cycles);
    void update_inputs();

    // DMA
    void sharc_dma_exec(int channel);
    void schedule_dma_op(int channel, std::uint32_t src, std::uint32_t dst, int src_modifier,
                         int dst_modifier, int src_count, int dst_count, int pmode);
    void schedule_chained_dma_op(int channel, std::uint32_t dma_chain_ptr, int chained_direction);
    void dma_run_cycle(int channel);
    void dma_op(int channel);

    void build_opcode_table();
    void op_unimplemented();

    // ---- Compute unit (ported verbatim from compute.hxx) -------------------
    SHARC_REG FADD(int fx, int fy);
    SHARC_REG FSUB(int fx, int fy);
    SHARC_REG FAVG(int fx, int fy);
    SHARC_REG FABS(int fx);
    SHARC_REG FMIN(int fx, int fy);
    SHARC_REG FMAX(int fx, int fy);
    std::pair<SHARC_REG, SHARC_REG> FADD_FSUB(int fx, int fy);
    uint32_t SCALB(SHARC_REG fx, int ry);
    SHARC_REG FMUL(int fx, int fy);
    void compute_add(int rn, int rx, int ry);
    void compute_sub(int rn, int rx, int ry);
    void compute_add_ci(int rn, int rx, int ry);
    void compute_sub_ci(int rn, int rx, int ry);
    void compute_comp(int rx, int ry);
    void compute_add_ci(int rn, int rx);
    void compute_sub_ci(int rn, int rx);
    void compute_inc(int rn, int rx);
    void compute_dec(int rn, int rx);
    void compute_neg(int rn, int rx);
    void compute_abs(int rn, int rx);
    void compute_pass(int rn, int rx);
    void compute_and(int rn, int rx, int ry);
    void compute_or(int rn, int rx, int ry);
    void compute_xor(int rn, int rx, int ry);
    void compute_not(int rn, int rx);
    void compute_min(int rn, int rx, int ry);
    void compute_max(int rn, int rx, int ry);
    void compute_clip(int rn, int rx, int ry);
    void compute_fadd(int fn, int fx, int fy);
    void compute_fsub(int fn, int fx, int fy);
    void compute_fadd_abs(int fn, int fx, int fy);
    void compute_fsub_abs(int fn, int fx, int fy);
    void compute_favg(int fn, int fx, int fy);
    void compute_fcomp(int fx, int fy);
    void compute_fneg(int fn, int fx);
    void compute_fabs(int fn, int fx);
    void compute_fpass(int fn, int fx);
    void compute_scalb(int fn, int fx, int ry);
    void compute_logb(int rn, int fx);
    void compute_fix_scaled(int rn, int fx, int ry);
    void compute_fix(int rn, int fx);
    void compute_float_scaled(int fn, int rx, int ry);
    void compute_float(int fn, int rx);
    void compute_recips(int fn, int fx);
    void compute_rsqrts(int fn, int fx);
    void compute_fcopysign(int fn, int fx, int fy);
    void compute_fmin(int fn, int fx, int fy);
    void compute_fmax(int fn, int fx, int fy);
    void compute_fclip(int fn, int fx, int fy);
    void compute_mul_uuin(int rn, int rx, int ry);
    void compute_mul_ssin(int rn, int rx, int ry);
    uint32_t compute_mrf_plus_mul_ssin(int rx, int ry);
    uint32_t compute_mrb_plus_mul_ssin(int rx, int ry);
    void compute_fmul(int fn, int fx, int fy);
    void compute_multi_mr_to_reg(int ai, int rk);
    void compute_multi_reg_to_mr(int ai, int rk);
    void compute_dual_add_sub(int ra, int rs, int rx, int ry);
    void compute_mul_ssfr_add(int rm, int rxm, int rym, int ra, int rxa, int rya);
    void compute_mul_ssfr_sub(int rm, int rxm, int rym, int ra, int rxa, int rya);
    void compute_dual_fadd_fsub(int fa, int fs, int fx, int fy);
    void compute_fmul_fadd(int fm, int fxm, int fym, int fa, int fxa, int fya);
    void compute_fmul_fsub(int fm, int fxm, int fym, int fa, int fxa, int fya);
    void compute_fmul_float_scaled(int fm, int fxm, int fym, int fa, int rxa, int rya);
    void compute_fmul_fix_scaled(int fm, int fxm, int fym, int ra, int fxa, int rya);
    void compute_fmul_favg(int fm, int fxm, int fym, int fa, int fxa, int fya);
    void compute_fmul_fabs(int fm, int fxm, int fym, int fa, int fxa);
    void compute_fmul_fmax(int fm, int fxm, int fym, int fa, int fxa, int fya);
    void compute_fmul_fmin(int fm, int fxm, int fym, int fa, int fxa, int fya);
    void compute_fmul_dual_fadd_fsub(int fm, int fxm, int fym, int fa, int fs, int fxa, int fya);

    // ---- Opcode handlers and helpers (ported from sharcops.hxx) -------------
    void add_systemreg_write_latency_effect(int sysreg, uint32_t data, uint32_t prev_data);
    void systemreg_write_latency_effect();
    uint32_t GET_UREG(int ureg);
    void SET_UREG(int ureg, uint32_t data);
    void SHIFT_OPERATION_IMM(int shiftop, int data, int rn, int rx);
    void COMPUTE(uint32_t opcode);
    void PUSH_PC();
    uint32_t POP_PC();
    uint32_t TOP_PC();
    void PUSH_LOOP();
    void POP_LOOP();
    void PUSH_STATUS_STACK();
    void POP_STATUS_STACK();
    int IF_CONDITION_CODE(int cond);
    int DO_CONDITION_CODE(int cond);
    void sharcop_compute_dreg_dm_dreg_pm();
    void sharcop_compute();
    void sharcop_compute_ureg_dmpm_premod();
    void sharcop_compute_ureg_dmpm_postmod();
    void sharcop_compute_dm_to_dreg_immmod();
    void sharcop_compute_dreg_to_dm_immmod();
    void sharcop_compute_pm_to_dreg_immmod();
    void sharcop_compute_dreg_to_pm_immmod();
    void sharcop_compute_ureg_to_ureg();
    void sharcop_imm_shift_dreg_dmpm();
    void sharcop_imm_shift();
    void sharcop_compute_modify();
    void sharcop_direct_call();
    void sharcop_direct_jump();
    void sharcop_relative_call();
    void sharcop_relative_jump();
    void sharcop_indirect_jump();
    void sharcop_indirect_call();
    void sharcop_relative_jump_compute();
    void sharcop_relative_call_compute();
    void sharcop_indirect_jump_compute_dreg_dm();
    void sharcop_relative_jump_compute_dreg_dm();
    void sharcop_rts();
    void sharcop_rti();
    void sharcop_do_until_counter_imm();
    void sharcop_do_until_counter_ureg();
    void sharcop_do_until();
    void sharcop_dm_to_ureg_direct();
    void sharcop_ureg_to_dm_direct();
    void sharcop_pm_to_ureg_direct();
    void sharcop_ureg_to_pm_direct();
    void sharcop_dm_to_ureg_indirect();
    void sharcop_ureg_to_dm_indirect();
    void sharcop_pm_to_ureg_indirect();
    void sharcop_ureg_to_pm_indirect();
    void sharcop_imm_to_dmpm();
    void sharcop_imm_to_ureg();
    void sharcop_sysreg_bitop();
    void sharcop_modify();
    void sharcop_bit_reverse();
    void sharcop_push_pop_stacks();
    void sharcop_nop();
    void sharcop_idle();
    void sharcop_unimplemented();
};

}  // namespace sage::cpu
