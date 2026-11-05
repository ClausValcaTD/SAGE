// Standalone Intel i960 disassembler.
//
// Ported from MAME's src/devices/cpu/i960/i960dis.cpp (BSD-3-Clause,
// copyright-holders: Farfetch'd, R. Belmont). The MAME util::stream_format
// helpers are replaced with a small local string builder.

#include "sage/cpu/i960.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace sage::cpu {
namespace {

// Sign-extends the low `bits` bits of `value` (MAME's util::sext).
constexpr std::int32_t sext(std::uint32_t value, int bits) {
    const std::uint32_t m = 1u << (bits - 1);
    return static_cast<std::int32_t>(((value & ((1u << bits) - 1)) ^ m) - m);
}

std::string fmt(const char* f, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

struct Mnemonic {
    const char* mnem;
    int type;
    signed char flags;
};

// Opcode table (index = top byte of the instruction word).
const Mnemonic kMnemonic[256] = {
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // 00
    {"b", 1, 1}, {"call", 1, 1}, {"ret", 1, 0}, {"bal", 1, 1}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"bno", 1, 1}, {"bg", 1, 1}, {"be", 1, 1}, {"bge", 1, 1}, {"bl", 1, 1}, {"bne", 1, 1}, {"ble", 1, 1}, {"bo", 1, 1}, // 10
    {"faultno", 1, 0}, {"faultg", 1, 0}, {"faulte", 1, 0}, {"faultge", 1, 0}, {"faultl", 1, 0}, {"faultne", 1, 0}, {"faultle", 1, 0}, {"faulto", 1, 0},

    {"testno", 2, 1}, {"testg", 2, 1}, {"teste", 2, 1}, {"testge", 2, 1}, {"testl", 2, 1}, {"testne", 2, 1}, {"testle", 2, 1}, {"testo", 2, 1}, // 20
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"bbc", 2, 3}, {"cmpobg", 2, 3}, {"cmpobe", 2, 3}, {"cmpobge", 2, 3}, {"cmpobl", 2, 3}, {"cmpobne", 2, 3}, {"cmpoble", 2, 3}, {"bbs", 2, 3}, // 30
    {"cmpibno", 2, 3}, {"cmpibg", 2, 3}, {"cmpibe", 2, 3}, {"cmpibge", 2, 3}, {"cmpibl", 2, 3}, {"cmpibne", 2, 3}, {"cmpible", 2, 3}, {"cmpibo", 2, 3},

    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // 40
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // 50
    {"58", 4, 0}, {"59", 4, 0}, {"5A", 4, 0}, {"5B", 4, 0}, {"5C", 4, 0}, {"5D", 4, 0}, {"5E", 4, 0}, {"5F", 4, 0},

    {"60", 4, 0}, {"61", 4, 0}, {"?", 0, 0}, {"63", 4, 0}, {"64", 4, 0}, {"65", 4, 0}, {"66", 4, 0}, {"67", 4, 0}, // 60
    {"68", 4, 0}, {"69", 4, 0}, {"?", 0, 0}, {"?", 0, 0}, {"6C", 4, 0}, {"6D", 4, 0}, {"6E", 4, 0}, {"?", 0, 0},

    {"70", 4, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"74", 4, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // 70
    {"78", 4, 0}, {"79", 4, 0}, {"7A", 4, 0}, {"7B", 4, 0}, {"7C", 4, 0}, {"7D", 4, 0}, {"7E", 4, 0}, {"7F", 4, 0},

    {"ldob", 3, 2}, {"?", 0, 0}, {"stob", 3, -2}, {"?", 0, 0}, {"bx", 3, 1}, {"balx", 3, 2}, {"callx", 3, 1}, {"?", 0, 0}, // 80
    {"ldos", 3, 2}, {"?", 0, 0}, {"stos", 3, -2}, {"?", 0, 0}, {"lda", 3, 2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"ld", 3, 2}, {"?", 0, 0}, {"st", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // 90
    {"ldl", 3, 2}, {"?", 0, 0}, {"stl", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"ldt", 3, 2}, {"?", 0, 0}, {"stt", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // a0
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"dcinva", 3, 1}, {"?", 0, 0}, {"?", 0, 0},

    {"ldq", 3, 2}, {"?", 0, 0}, {"stq", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // b0
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"ldib", 3, 2}, {"?", 0, 0}, {"stib", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // c0
    {"ldis", 3, 2}, {"?", 0, 0}, {"stis", 3, -2}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // d0
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // e0
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0},

    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, // f0
    {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}, {"?", 0, 0}};

struct MnemReg {
    const char* mnem;
    std::uint16_t type;
    signed char flags;
};

const MnemReg kMnemReg[] = {
    {"notbit", 0x580, -3}, {"and", 0x581, -3}, {"andnot", 0x582, -3}, {"setbit", 0x583, -3}, // 58
    {"notand", 0x584, -3}, {"xor", 0x586, -3}, {"or", 0x587, -3},
    {"nor", 0x588, -3}, {"xnor", 0x589, -3}, {"not", 0x58a, -2}, {"ornot", 0x58b, -3},
    {"clrbit", 0x58c, -3}, {"notor", 0x58d, -3}, {"nand", 0x58e, -3}, {"alterbit", 0x58f, -3},

    {"addo", 0x590, -3}, {"addi", 0x591, -3}, {"subo", 0x592, -3}, {"subi", 0x593, -3}, // 59
    {"cmpob", 0x594, 2}, {"cmpib", 0x595, 2}, {"cmpos", 0x596, 2}, {"cmpis", 0x597, 2},
    {"shro", 0x598, -3}, {"shrdi", 0x59a, -3}, {"shri", 0x59b, -3},
    {"shlo", 0x59c, -3}, {"rotate", 0x59d, -3}, {"shli", 0x59e, -3},

    {"cmpo", 0x5a0, 2}, {"cmpi", 0x5a1, 2}, {"concmpo", 0x5a2, 2}, {"concmpi", 0x5a3, 2}, // 5a
    {"cmpinco", 0x5a4, -3}, {"cmpinci", 0x5a5, -3}, {"cmpdeco", 0x5a6, -3}, {"cmpdeci", 0x5a7, -3},
    {"scanbyte", 0x5ac, 2}, {"bswap", 0x5ad, -2}, {"chkbit", 0x5ae, 2},

    {"addc", 0x5b0, -3}, {"subc", 0x5b2, -3}, {"intdis", 0x5b4, 0}, {"inten", 0x5b5, 0}, // 5b

    {"mov", 0x5cc, -2}, // 5c

    {"eshro", 0x5d8, -3}, // 5d
    {"movl", 0x5dc, -2},

    {"movt", 0x5ec, -2}, // 5e

    {"movq", 0x5fc, -2}, // 5f

    {"synmov", 0x600, 2}, {"synmovl", 0x601, 2}, {"synmovq", 0x602, 2}, {"cmpstr", 0x603, 3}, // 60
    {"movqstr", 0x604, -3}, {"movstr", 0x605, -3},

    {"atmod", 0x610, 33}, {"atadd", 0x612, 33}, {"inspacc", 0x613, -2}, // 61
    {"ldphy", 0x614, -2}, {"synld", 0x615, -2}, {"fill", 0x617, 3},

    {"sdma", 0x630, 3}, {"udma", 0x631, 0}, // 63

    {"spanbit", 0x640, -2}, {"scanbit", 0x641, -2}, {"daddc", 0x642, -3}, {"dsubc", 0x643, -3}, // 64
    {"dmovt", 0x644, -2}, {"modac", 0x645, 3},

    {"modify", 0x650, 33}, {"extract", 0x651, 33}, // 65
    {"modtc", 0x654, 33}, {"modpc", 0x655, 33}, {"receive", 0x656, -2},
    {"intctl", 0x658, -2}, {"sysctl", 0x659, 33}, {"icctl", 0x65b, 33},
    {"dcctl", 0x65c, 33}, {"halt", 0x65d, 0},

    {"calls", 0x660, 1}, {"send", 0x662, -3}, {"sendserv", 0x663, 1}, // 66
    {"resumprcs", 0x664, 1}, {"schedprcs", 0x665, 1}, {"saveprcs", 0x666, 0},
    {"condwait", 0x668, 1}, {"wait", 0x669, 1}, {"signal", 0x66a, 1}, {"mark", 0x66b, 0},
    {"fmark", 0x66c, 0}, {"flushreg", 0x66d, 0}, {"syncf", 0x66f, 0},

    {"emul", 0x670, -3}, {"ediv", 0x671, -3}, {"ldtime", 0x671, -1}, // 67
    {"cvtir", 0x674, -20}, {"cvtilr", 0x675, -20}, {"scalerl", 0x676, -30}, {"scaler", 0x677, -30},

    {"atanr", 0x680, -30}, {"logepr", 0x681, -30}, {"logr", 0x682, -30}, {"remr", 0x683, -30}, // 68
    {"cmpor", 0x684, 20}, {"cmpr", 0x685, 20},
    {"sqrtr", 0x688, -20}, {"expr", 0x689, -20}, {"logbnr", 0x68a, -20}, {"roundr", 0x68b, -20},
    {"sinr", 0x68c, -20}, {"cosr", 0x68d, -20}, {"tanr", 0x68e, -20}, {"classr", 0x68f, 10},

    {"atanrl", 0x690, -30}, {"logeprl", 0x691, -30}, {"logrl", 0x692, -30}, {"remrl", 0x693, -30}, // 69
    {"cmporl", 0x694, 20}, {"cmprl", 0x695, 20},
    {"sqrtrl", 0x698, -20}, {"exprl", 0x699, -20}, {"logbnrl", 0x69a, -20}, {"roundrl", 0x69b, -20},
    {"sinrl", 0x69c, -20}, {"cosrl", 0x69d, -20}, {"tanrl", 0x69e, -20}, {"classrl", 0x69f, 10},

    {"cvtri", 0x6c0, -20}, {"cvtril", 0x6c1, -20}, {"cvtzri", 0x6c2, -20}, {"cvtzril", 0x6c3, -20}, // 6c
    {"movr", 0x6c9, -20},

    {"movrl", 0x6d9, -20}, // 6d

    {"movre", 0x6e1, -20}, {"cpysre", 0x6e2, -30}, {"cpyrsre", 0x6e3, -30}, // 6e
    {"movre", 0x6e9, -20},

    {"mulo", 0x701, -3}, // 70
    {"remo", 0x708, -3}, {"divo", 0x70b, -3},

    {"muli", 0x741, -3}, // 74
    {"remi", 0x748, -3}, {"modi", 0x749, -3}, {"divi", 0x74b, -3},

    {"addono", 0x780, -3}, {"addino", 0x781, -3}, {"subono", 0x782, -3}, {"subino", 0x783, -3}, // 78
    {"selno", 0x784, -3},
    {"divr", 0x78b, -30}, {"mulr", 0x78c, -30}, {"subr", 0x78d, -30}, {"addr", 0x78f, -30},

    {"addog", 0x790, -3}, {"addig", 0x791, -3}, {"subog", 0x792, -3}, {"subig", 0x793, -3}, // 79
    {"selg", 0x794, -3},
    {"divrl", 0x79b, -30}, {"mulrl", 0x79c, -30}, {"subrl", 0x79d, -30}, {"addrl", 0x79f, -30},

    {"addoe", 0x7a0, -3}, {"addie", 0x7a1, -3}, {"suboe", 0x7a2, -3}, {"subie", 0x7a3, -3}, // 7a
    {"sele", 0x7a4, -3},

    {"addoge", 0x7b0, -3}, {"addige", 0x7b1, -3}, {"suboge", 0x7b2, -3}, {"subige", 0x7b3, -3}, // 7b
    {"selge", 0x7b4, -3},

    {"addol", 0x7c0, -3}, {"addil", 0x7c1, -3}, {"subol", 0x7c2, -3}, {"subil", 0x7c3, -3}, // 7c
    {"sell", 0x7c4, -3},

    {"addone", 0x7d0, -3}, {"addine", 0x7d1, -3}, {"subone", 0x7d2, -3}, {"subine", 0x7d3, -3}, // 7d
    {"selne", 0x7d4, -3},

    {"addole", 0x7e0, -3}, {"addile", 0x7e1, -3}, {"subole", 0x7e2, -3}, {"subile", 0x7e3, -3}, // 7e
    {"selle", 0x7e4, -3},

    {"addoo", 0x7f0, -3}, {"addio", 0x7f1, -3}, {"suboo", 0x7f2, -3}, {"subio", 0x7f3, -3}, // 7f
    {"selo", 0x7f4, -3},

    {"ending_code", 0, 0}};

const char* const kRegNames[32] = {
    "pfp", "sp", "rip", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8", "g9", "g10", "g11", "g12", "g13", "g14", "fp",
};

const char* const kFprNames[32] = {
    "fp0", "fp1", "fp2", "fp3", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?",
    "+0.0", "?", "?", "?", "?", "?", "+1.0", "?", "?", "?", "?", "?", "?", "?", "?", "?",
};

std::string decode_invalid(std::uint32_t iCode) { return fmt("? %08x", iCode); }

std::string decode_ctrl(std::uint32_t iCode, std::uint32_t ip, signed char cnt) {
    const std::uint8_t op = static_cast<std::uint8_t>(iCode >> 24);
    const std::uint32_t disp = iCode & 0x00fffffc;

    if ((iCode & 1) != 0)
        return decode_invalid(iCode);

    switch (cnt) {
        case 0:
            return kMnemonic[op].mnem;
        case 1:
            return fmt("%-8s0x%08x", kMnemonic[op].mnem,
                       static_cast<std::uint32_t>(sext(disp, 24) + ip));
        default:
            return decode_invalid(iCode);
    }
}

std::string decode_cobr(std::uint32_t iCode, std::uint32_t ip, signed char cnt) {
    const std::uint8_t op = static_cast<std::uint8_t>(iCode >> 24);
    const std::uint8_t src1 = static_cast<std::uint8_t>(iCode >> 19) & 0x1f;
    const std::uint8_t src2 = static_cast<std::uint8_t>(iCode >> 14) & 0x1f;
    const std::uint8_t m1 = static_cast<std::uint8_t>(iCode >> 13 & 1);
    const std::uint8_t s2 = static_cast<std::uint8_t>(iCode & 1);
    const std::uint32_t disp = iCode & 0x1ffc;

    std::string op1, op2, op3;

    switch (cnt) {
        case 1:
            return fmt("%-8s%s", kMnemonic[op].mnem, kRegNames[src1]);
        case 3:
            op1 = m1 ? fmt("%d", src1) : fmt("%s", kRegNames[src1]);
            op2 = s2 ? fmt("sf%d", src2) : fmt("%s", kRegNames[src2]);
            op3 = fmt("0x%x", static_cast<std::uint32_t>(sext(disp, 13) + ip));
            return fmt("%-8s%s,%s,%s", kMnemonic[op].mnem, op1.c_str(), op2.c_str(), op3.c_str());
        default:
            return decode_invalid(iCode);
    }
}

std::string decode_mema(std::uint32_t iCode, signed char cnt) {
    const std::uint8_t op = static_cast<std::uint8_t>(iCode >> 24);
    const std::uint8_t srcdst = static_cast<std::uint8_t>(iCode >> 19) & 0x1f;
    const std::uint8_t abase = static_cast<std::uint8_t>(iCode >> 14) & 0x1f;
    const std::uint8_t mode = static_cast<std::uint8_t>(iCode >> 13) & 0x1;
    const std::uint32_t offset = iCode & 0xfff;

    switch (mode) {
        case 0:
            switch (cnt) {
                case 1:
                    return fmt("%-8s0x%x", kMnemonic[op].mnem, offset);
                case 2:
                    return fmt("%-8s0x%x,%s", kMnemonic[op].mnem, offset, kRegNames[srcdst]);
                case -2:
                    return fmt("%-8s%s,0x%x", kMnemonic[op].mnem, kRegNames[srcdst], offset);
                default:
                    return decode_invalid(iCode);
            }
        case 1:
            switch (cnt) {
                case 1:
                    return fmt("%-8s0x%x(%s)", kMnemonic[op].mnem, offset, kRegNames[abase]);
                case 2:
                    return fmt("%-8s0x%x(%s),%s", kMnemonic[op].mnem, offset, kRegNames[abase],
                               kRegNames[srcdst]);
                case -2:
                    return fmt("%-8s%s,0x%x(%s)", kMnemonic[op].mnem, kRegNames[srcdst], offset,
                               kRegNames[abase]);
                default:
                    return decode_invalid(iCode);
            }
        default:
            return decode_invalid(iCode);
    }
}

std::string decode_memb(std::uint32_t iCode, std::uint32_t ip, std::uint32_t disp, signed char cnt) {
    const std::uint8_t op = static_cast<std::uint8_t>(iCode >> 24);
    const std::uint8_t srcdst = static_cast<std::uint8_t>(iCode >> 19) & 0x1f;
    const std::uint8_t abase = static_cast<std::uint8_t>(iCode >> 14) & 0x1f;
    const std::uint8_t mode = static_cast<std::uint8_t>(iCode >> 10) & 0xf;
    const std::uint8_t scale = static_cast<std::uint8_t>(iCode >> 7) & 0x7;
    const std::uint8_t index = static_cast<std::uint8_t>(iCode) & 0x1f;

    std::string efa;

    if ((iCode & 0x60) != 0x0)
        return decode_invalid(iCode);
    if (scale > 4)
        return decode_invalid(iCode);

    switch (mode) {
        case 0x4:
            efa = fmt("(%s)", kRegNames[abase]);
            break;
        case 0x5:
            efa = fmt("0x%x", ip + disp + 8);
            break;
        case 0x6:
            return decode_invalid(iCode);
        case 0x7:
            efa = (scale == 0) ? fmt("(%s)[%s]", kRegNames[abase], kRegNames[index])
                               : fmt("(%s)[%s*%d]", kRegNames[abase], kRegNames[index], 1 << scale);
            break;
        case 0xc:
            efa = fmt("0x%x", disp);
            break;
        case 0xd:
            efa = fmt("0x%x(%s)", disp, kRegNames[abase]);
            break;
        case 0xe:
            efa = (scale == 0) ? fmt("0x%x[%s]", disp, kRegNames[index])
                               : fmt("0x%x[%s*%d]", disp, kRegNames[index], 1 << scale);
            break;
        case 0xf:
            efa = (scale == 0) ? fmt("0x%x(%s)[%s]", disp, kRegNames[abase], kRegNames[index])
                               : fmt("0x%x(%s)[%s*%d]", disp, kRegNames[abase], kRegNames[index],
                                     1 << scale);
            break;
        default:
            return decode_invalid(iCode);
    }

    switch (cnt) {
        case 1:
            return fmt("%-8s%s", kMnemonic[op].mnem, efa.c_str());
        case 2:
            return fmt("%-8s%s,%s", kMnemonic[op].mnem, efa.c_str(), kRegNames[srcdst]);
        case -2:
            return fmt("%-8s%s,%s", kMnemonic[op].mnem, kRegNames[srcdst], efa.c_str());
        default:
            return decode_invalid(iCode);
    }
}

std::string decode_reg(std::uint32_t iCode) {
    const std::uint16_t op =
        static_cast<std::uint16_t>(((iCode >> 20) & 0xff0) | ((iCode >> 7) & 0xf));
    const std::uint8_t srcdst = static_cast<std::uint8_t>(iCode >> 19) & 0x1f;
    const std::uint8_t src2 = static_cast<std::uint8_t>(iCode >> 14) & 0x1f;
    const std::uint8_t m3 = static_cast<std::uint8_t>(iCode >> 13) & 1;
    const std::uint8_t m2 = static_cast<std::uint8_t>(iCode >> 12) & 1;
    const std::uint8_t m1 = static_cast<std::uint8_t>(iCode >> 11) & 1;
    const std::uint8_t s2 = static_cast<std::uint8_t>(iCode >> 6) & 1;
    const std::uint8_t s1 = static_cast<std::uint8_t>(iCode >> 5) & 1;
    const std::uint8_t src1 = static_cast<std::uint8_t>(iCode) & 0x1f;

    const std::uint8_t sm1 = static_cast<std::uint8_t>((s1 << 1) | m1);
    const std::uint8_t sm2 = static_cast<std::uint8_t>((s2 << 1) | m2);
    std::size_t i = 0;
    std::string op1, op2, op3;

    while (kMnemReg[i].type != 0) {
        if (kMnemReg[i].type == op)
            break;
        i++;
    }
    if (kMnemReg[i].type != op)
        return decode_invalid(iCode);

    switch (sm1) {
        case 0: op1 = fmt("%s", kRegNames[src1]); break;
        case 1: op1 = fmt("%d", src1); break;
        case 2: op1 = fmt("sf%d", src1); break;
        default: return decode_invalid(iCode);
    }

    switch (sm2) {
        case 0: op2 = fmt("%s", kRegNames[src2]); break;
        case 1: op2 = fmt("%d", src2); break;
        case 2: op2 = fmt("sf%d", src2); break;
        default: return decode_invalid(iCode);
    }

    switch (kMnemReg[i].flags) {
        case 0:
            return fmt("%-8s", kMnemReg[i].mnem);
        case 1:
            return fmt("%-8s%s", kMnemReg[i].mnem, op1.c_str());
        case -1:
            op3 = m3 ? fmt("sf%d", srcdst) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s", kMnemReg[i].mnem, op3.c_str());
        case 2:
            return fmt("%-8s%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str());
        case -2:
            op3 = m3 ? fmt("sf%d", srcdst) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s", kMnemReg[i].mnem, op1.c_str(), op3.c_str());
        case 3:
            op3 = m3 ? fmt("%d", srcdst) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str(), op3.c_str());
        case -3:
            op3 = m3 ? fmt("sf%d", srcdst) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str(), op3.c_str());
        case 33:
            if (m3)
                return decode_invalid(iCode);
            op3 = fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str(), op3.c_str());

        // floating-point operand forms
        case 10:
            op1 = m1 ? fmt("%s", kFprNames[src1]) : fmt("%s", kRegNames[src1]);
            return fmt("%-8s%s", kMnemReg[i].mnem, op1.c_str());
        case 20:
            op1 = m1 ? fmt("%s", kFprNames[src1]) : fmt("%s", kRegNames[src1]);
            op2 = m2 ? fmt("%s", kFprNames[src2]) : fmt("%s", kRegNames[src2]);
            return fmt("%-8s%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str());
        case -20:
            op1 = m1 ? fmt("%s", kFprNames[src1]) : fmt("%s", kRegNames[src1]);
            op3 = m3 ? fmt("%s", kFprNames[srcdst]) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s", kMnemReg[i].mnem, op1.c_str(), op3.c_str());
        case -30:
            op1 = m1 ? fmt("%s", kFprNames[src1]) : fmt("%s", kRegNames[src1]);
            op2 = m2 ? fmt("%s", kFprNames[src2]) : fmt("%s", kRegNames[src2]);
            op3 = m3 ? fmt("%s", kFprNames[srcdst]) : fmt("%s", kRegNames[srcdst]);
            return fmt("%-8s%s,%s,%s", kMnemReg[i].mnem, op1.c_str(), op2.c_str(), op3.c_str());
        default:
            return decode_invalid(iCode);
    }
}

}  // namespace

std::string disassemble_i960(const BusInterface& bus, std::uint32_t pc) {
    const std::uint32_t iCode = bus.read32(pc);
    const std::uint8_t op = static_cast<std::uint8_t>(iCode >> 24);

    switch (kMnemonic[op].type) {
        case 0:
            return decode_invalid(iCode);
        case 1:
            return decode_ctrl(iCode, pc, kMnemonic[op].flags);
        case 2:
            return decode_cobr(iCode, pc, kMnemonic[op].flags);
        case 3:
            if ((iCode >> 12) & 0x1)
                return decode_memb(iCode, pc, bus.read32(pc + 4), kMnemonic[op].flags);
            return decode_mema(iCode, kMnemonic[op].flags);
        case 4:
            return decode_reg(iCode);
        default:
            return "???";
    }
}

}  // namespace sage::cpu
