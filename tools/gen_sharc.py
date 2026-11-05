#!/usr/bin/env python3
"""Regenerate the SHARC translation units from the MAME reference sources.

Reads references/sharc/{compute,sharcops,sharcdma}.hxx and sharcops_table.cpp and
writes the *.inc files under src/cpu/ that sharc.cpp includes.
Run from the repo root: python3 tools/gen_sharc.py
"""
import os
import re

REF = os.path.join(os.path.dirname(__file__), '..', 'references', 'sharc')
OUT = os.path.join(os.path.dirname(__file__), '..', 'src', 'cpu')


def read(name):
    with open(os.path.join(REF, name)) as f:
        return f.read()


def transform(txt):
    txt = txt.replace('adsp21062_device::', 'Sharc::')
    txt = txt.replace('emu_fatalerror', 'SharcFault')
    txt = re.sub(r'(?<![A-Za-z_])fatalerror\(', 'throw SharcFault(', txt)
    txt = txt.replace('util::sext(', 'sext(')
    txt = txt.replace('util::make_bitmask<', 'make_bitmask<')
    txt = txt.replace('util::make_bitmask(', 'make_bitmask(')
    txt = txt.replace('ACCESSING_BITS_0_15', 'true')
    txt = txt.replace('m_dm_short_view.select(0)', 'm_sse = true')
    txt = txt.replace('m_dm_short_view.disable()', 'm_sse = false')
    txt = txt.replace('m_program.read_qword(', 'pm_read48_raw(')
    txt = txt.replace('m_data.write_dword(', 'dm_write32(')
    txt = txt.replace('m_data.read_dword(', 'dm_read32_raw(')
    txt = txt.replace('disassemble_one(', 'disassemble(')
    txt = txt.replace('m_flag_out_cb[i](', 'm_flag_out_value[i] = (')
    txt = txt.replace(
        'm_core->dma_op[channel].timer->adjust(cycles_to_attotime(cycles), channel);',
        'm_core->dma_op[channel].timer = cycles;')
    txt = txt.replace('m_core->dma_op[6].timer->adjust(attotime::never, 0);',
                      'm_core->dma_op[6].timer = 0;')
    txt = txt.replace('#include "compute.hxx"', '#include "sharc_compute.inc"')
    return txt


PRELUDE = '''
// ---- constants and helpers extracted from MAME sharcinternal.ipp / eminline.h
constexpr uint32_t FLOAT_CANONICAL_NAN   = 0xffffffff;
constexpr uint32_t FLOAT_INFINITY        = 0x7f800000;
constexpr uint32_t FLOAT_SIGN_MASK       = 0x80000000;
constexpr uint32_t FLOAT_EXPONENT_MASK   = 0x7f800000;
constexpr uint32_t FLOAT_MANTISSA_MASK   = 0x007fffff;
constexpr unsigned FLOAT_SIGN_SHIFT      = 31;
constexpr unsigned FLOAT_EXPONENT_SHIFT  = 23;
constexpr unsigned FLOAT_EXPONENT_BITS   = 8;
constexpr int      FLOAT_EXPONENT_BIAS   = 127;

constexpr int float_get_unbiased_exponent(uint32_t f) {
    return int(int32_t(BIT(f, FLOAT_EXPONENT_SHIFT, FLOAT_EXPONENT_BITS)) - FLOAT_EXPONENT_BIAS);
}
constexpr uint32_t float_make_biased_exponent(int exponent) {
    return uint32_t((exponent + FLOAT_EXPONENT_BIAS) & make_bitmask<int>(FLOAT_EXPONENT_BITS)) << FLOAT_EXPONENT_SHIFT;
}

constexpr int64_t  mul_32x32(int32_t a, int32_t b)             { return int64_t(a) * int64_t(b); }
constexpr uint64_t mulu_32x32(uint32_t a, uint32_t b)          { return uint64_t(a) * uint64_t(b); }
constexpr int32_t  mul_32x32_shift(int32_t a, int32_t b, uint8_t shift) { return int32_t((int64_t(a) * int64_t(b)) >> shift); }
constexpr uint32_t mulu_32x32_shift(uint32_t a, uint32_t b, uint8_t shift) { return uint32_t((uint64_t(a) * uint64_t(b)) >> shift); }

constexpr unsigned  op_get_subop(uint64_t opcode)     { return unsigned(BIT(opcode, 40, 5)); }
constexpr uint32_t  op_get_compute(uint64_t opcode)   { return uint32_t(BIT(opcode, 0, 23)); }
constexpr unsigned  op_get_cond(uint64_t opcode)      { return unsigned(BIT(opcode, 33, 5)); }
constexpr unsigned  op_get_ureg_src(uint64_t opcode)  { return unsigned(BIT(opcode, 36, 8)); }
constexpr unsigned  op_get_cond_ureg(uint64_t opcode) { return unsigned(BIT(opcode, 31, 5)); }
constexpr unsigned  op_get_ureg_dst(uint64_t opcode)  { return unsigned(BIT(opcode, 23, 8)); }
constexpr int       op_get_reladdr(uint64_t opcode)   { return sext(unsigned(opcode >> 27), 6); }
constexpr unsigned  op_get_dmi(uint64_t opcode)       { return unsigned(BIT(opcode, 41, 3)); }
constexpr unsigned  op_get_dmm(uint64_t opcode)       { return unsigned(BIT(opcode, 38, 3)); }
constexpr unsigned  op_get_pmi(uint64_t opcode)       { return unsigned(BIT(opcode, 30, 3)); }
constexpr unsigned  op_get_pmm(uint64_t opcode)       { return unsigned(BIT(opcode, 27, 3)); }
constexpr unsigned  op_get_jump_b(uint64_t opcode)    { return unsigned(BIT(opcode, 39)); }
constexpr unsigned  op_get_jump_la(uint64_t opcode)   { return unsigned(BIT(opcode, 38)); }
constexpr unsigned  op_get_jump_j(uint64_t opcode)    { return unsigned(BIT(opcode, 26)); }
constexpr unsigned  op_get_jump_e(uint64_t opcode)    { return unsigned(BIT(opcode, 25)); }
constexpr unsigned  op_get_jump_ci(uint64_t opcode)   { return unsigned(BIT(opcode, 24)); }
constexpr unsigned  op_get_rs(uint64_t opcode)        { return unsigned(BIT(opcode, 12, 4)); }
constexpr unsigned  op_get_ra(uint64_t opcode)        { return unsigned(BIT(opcode, 8, 4)); }

'''

HDR = ('// Generated from references/sharc/%s (MAME, Ville Linde, BSD-3-Clause).\n'
       '// Do not edit by hand; regenerate with tools/gen_sharc.py.\n')


def main():
    ops = transform(read('sharcops.hxx'))
    open(os.path.join(OUT, 'sharc_ops.inc'), 'w').write(HDR % 'sharcops.hxx' + PRELUDE + ops)

    compute = transform(read('compute.hxx'))
    open(os.path.join(OUT, 'sharc_compute.inc'), 'w').write(HDR % 'compute.hxx' + compute)

    dma = transform(read('sharcdma.hxx'))
    i = dma.find('TIMER_CALLBACK_MEMBER(Sharc::sharc_dma_callback)')
    if i >= 0:
        end = dma.find('\n}\n', i) + 3
        dma = dma[:i] + dma[end:]
    open(os.path.join(OUT, 'sharc_dma.inc'), 'w').write(HDR % 'sharcdma.hxx' + dma)

    table = read('sharcops_table.cpp')
    table = table.replace('adsp21062_device::', 'Sharc::')
    table = table.replace('#include "emu.h"\n#include "sharc.h"\n', '')
    open(os.path.join(OUT, 'sharc_opcode_table.inc'), 'w').write(HDR % 'sharcops_table.cpp' + table)

    print('generated sharc_ops.inc, sharc_compute.inc, sharc_dma.inc, sharc_opcode_table.inc')


if __name__ == '__main__':
    main()
