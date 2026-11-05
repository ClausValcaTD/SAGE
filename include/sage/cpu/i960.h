#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace sage::cpu {

// Memory interconnect seen by the CPU. All accesses are little-endian.
class BusInterface {
public:
    virtual ~BusInterface() = default;
    virtual std::uint8_t read8(std::uint32_t addr) const = 0;
    virtual std::uint16_t read16(std::uint32_t addr) const = 0;
    virtual std::uint32_t read32(std::uint32_t addr) const = 0;
    virtual void write8(std::uint32_t addr, std::uint8_t val) = 0;
    virtual void write16(std::uint32_t addr, std::uint16_t val) = 0;
    virtual void write32(std::uint32_t addr, std::uint32_t val) = 0;

    // True where the target supports i960 burst transfers (plain ROM/RAM).
    // Mirrors MAME's per-map-entry `flags(i960_cpu_device::BURST)`; register
    // and FIFO apertures return false so multi-word loads re-read one address.
    virtual bool burst(std::uint32_t /*addr*/) const { return true; }
};

// Raised for architectural conditions the core does not (yet) implement.
// Ported from MAME's fatalerror() call sites so a bad decode is recoverable
// instead of terminating the process.
class Fault : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Standalone Intel i960 (80960KB/KA family) interpreter, ported from MAME's
// src/devices/cpu/i960/i960.cpp with the MAME device framework removed.
//
// Register file layout (m_r indices), matching the hardware:
//   0..15   local registers r0..r15   (0=pfp, 1=sp, 2=rip)
//   16..31  global registers g0..g15  (31=fp)
class I960 {
public:
    static constexpr int kRcacheSize = 4;
    static constexpr std::uint16_t kBurst = 0x0001;

    explicit I960(BusInterface& bus);

    // Reads SAT/PRCB/IP from the reset vector at 0x0, sets the supervisor
    // control word and establishes the initial frame/stack pointers.
    void reset();

    // Executes exactly one instruction. Returns the cycle count consumed.
    // Throws Fault if the opcode stream hits an unimplemented case.
    int step();

    // Renders the instruction at `pc` as text (no side effects).
    std::string disassemble(std::uint32_t pc) const;

    std::uint32_t get_ip() const { return m_IP; }
    std::uint32_t get_pip() const { return m_PIP; }
    std::uint32_t get_pc() const { return m_PC; }
    std::uint32_t get_ac() const { return m_AC; }
    std::uint32_t get_sat() const { return m_SAT; }
    std::uint32_t get_prcb() const { return m_PRCB; }
    std::uint32_t get_icr() const { return m_ICR; }
    std::uint32_t reg(int i) const { return m_r[i]; }

    // Interrupt line state (0..3); kept for parity with the MAME core.
    void set_input(int irqline, int state);

private:
    // Memory helpers mirroring MAME's memory_access<> accessors.
    std::uint32_t read_dword_unaligned(std::uint32_t address) const;
    std::pair<std::uint32_t, std::uint16_t> read_dword_unaligned_flags(std::uint32_t address) const;
    std::uint16_t read_word_unaligned(std::uint32_t address) const;
    void write_dword_unaligned(std::uint32_t address, std::uint32_t data);
    std::uint16_t write_dword_unaligned_flags(std::uint32_t address, std::uint32_t data);
    void write_word_unaligned(std::uint32_t address, std::uint16_t data);

    void send_iac(std::uint32_t adr);
    std::uint32_t get_ea(std::uint32_t opcode);
    std::uint32_t get_1_ri(std::uint32_t opcode);
    std::uint32_t get_2_ri(std::uint32_t opcode);
    std::uint64_t get_2_ri64(std::uint32_t opcode);
    void set_ri(std::uint32_t opcode, std::uint32_t val);
    void set_ri2(std::uint32_t opcode, std::uint32_t val, std::uint32_t val2);
    void set_ri64(std::uint32_t opcode, std::uint64_t val);
    double get_1_rif(std::uint32_t opcode);
    double get_2_rif(std::uint32_t opcode);
    void set_rif(std::uint32_t opcode, double val);
    double get_1_rifl(std::uint32_t opcode);
    double get_2_rifl(std::uint32_t opcode);
    void set_rifl(std::uint32_t opcode, double val);
    std::uint32_t get_1_ci(std::uint32_t opcode);
    std::uint32_t get_2_ci(std::uint32_t opcode);
    std::uint32_t get_disp(std::uint32_t opcode);
    std::uint32_t get_disp_s(std::uint32_t opcode);
    void cmp_s(std::int32_t v1, std::int32_t v2);
    void cmp_u(std::uint32_t v1, std::uint32_t v2);
    void concmp_s(std::int32_t v1, std::int32_t v2);
    void concmp_u(std::uint32_t v1, std::uint32_t v2);
    void cmp_d(double v1, double v2);
    void bxx(std::uint32_t opcode, int mask);
    void bxx_s(std::uint32_t opcode, int mask);
    void fxx(std::uint32_t opcode, int mask);
    void test(std::uint32_t opcode, int mask);
    double round_to_int(double val);
    void execute_op(std::uint32_t opcode);
    void execute_burst_stall_op(std::uint32_t opcode);
    void take_interrupt(int vector, int lvl);
    void check_immediate_irqs();
    void check_pending_irqs();
    void do_call(std::uint32_t adr, int type, std::uint32_t stack);
    void do_ret_0();
    void do_ret();

    struct StallState {
        std::uint32_t t1 = 0, t2 = 0;
        int index = 0, size = 0;
        bool burst_mode = false;
        bool iswriteop = false;
    };

    BusInterface& m_bus;

    StallState m_stall_state;
    bool m_stalled = false;

    std::uint32_t m_r[0x20]{};
    std::uint32_t m_rcache[kRcacheSize][0x10]{};
    std::uint32_t m_rcache_frame_addr[kRcacheSize]{};
    int m_rcache_pos = 0;

    double m_fp[4]{};

    std::uint32_t m_SAT = 0;
    std::uint32_t m_PRCB = 0;
    std::uint32_t m_PC = 0;
    std::uint32_t m_AC = 0;
    std::uint32_t m_IP = 0;
    std::uint32_t m_PIP = 0;
    std::uint32_t m_ICR = 0;

    int m_immediate_irq = 0;
    int m_immediate_vector = 0;
    int m_immediate_pri = 0;

    std::int8_t m_irq_line_state[4]{};

    int m_icount = 0;
};

// Standalone disassembler (ported from MAME's i960dis.cpp).
std::string disassemble_i960(const BusInterface& bus, std::uint32_t pc);

}  // namespace sage::cpu
