// Generated from references/sharc/sharcops_table.cpp (MAME, Ville Linde, BSD-3-Clause).
// Do not edit by hand; regenerate with tools/gen_sharc.py.
// license:BSD-3-Clause
// copyright-holders:Ville Linde


const Sharc::SHARC_OP Sharc::s_sharc_opcode_table[] =
{
	//  |0 0 1|
	{   0xe000,     0x2000,     &Sharc::sharcop_compute_dreg_dm_dreg_pm                     },

	//  |0 0 0|0 0 0 0 1|
	{   0xff00,     0x0100,     &Sharc::sharcop_compute                                     },

	//  |0 1 0|0|
	{   0xf000,     0x4000,     &Sharc::sharcop_compute_ureg_dmpm_premod                    },

	//  |0 1 0|1|
	{   0xf000,     0x5000,     &Sharc::sharcop_compute_ureg_dmpm_postmod                   },

	//  |0 1 1|0|x x x|0|0|
	{   0xf180,     0x6000,     &Sharc::sharcop_compute_dm_to_dreg_immmod                   },

	//  |0 1 1|0|x x x|0|1|
	{   0xf180,     0x6080,     &Sharc::sharcop_compute_dreg_to_dm_immmod                   },

	//  |0 1 1|0|x x x|1|0|
	{   0xf180,     0x6100,     &Sharc::sharcop_compute_pm_to_dreg_immmod                   },

	//  |0 1 1|0|x x x|1|1|
	{   0xf180,     0x6180,     &Sharc::sharcop_compute_dreg_to_pm_immmod                   },

	//  |0 1 1|1|
	{   0xf000,     0x7000,     &Sharc::sharcop_compute_ureg_to_ureg                        },

	//  |1 0 0|0|
	{   0xf000,     0x8000,     &Sharc::sharcop_imm_shift_dreg_dmpm                         },

	//  |0 0 0|0 0 0 1 0|
	{   0xff00,     0x0200,     &Sharc::sharcop_imm_shift                                   },

	//  |0 0 0|0 0 1 0 0|
	{   0xff00,     0x0400,     &Sharc::sharcop_compute_modify                              },

	//  |0 0 0|0 0 1 1 0|0|
	{   0xff80,     0x0600,     &Sharc::sharcop_direct_jump                                 },

	//  |0 0 0|0 0 1 1 0|1|
	{   0xff80,     0x0680,     &Sharc::sharcop_direct_call                                 },

	//  |0 0 0|0 0 1 1 1|0|
	{   0xff80,     0x0700,     &Sharc::sharcop_relative_jump                               },

	//  |0 0 0|0 0 1 1 1|1|
	{   0xff80,     0x0780,     &Sharc::sharcop_relative_call                               },

	//  |0 0 0|0 1 0 0 0|0|
	{   0xff80,     0x0800,     &Sharc::sharcop_indirect_jump                               },

	//  |0 0 0|0 1 0 0 0|1|
	{   0xff80,     0x0880,     &Sharc::sharcop_indirect_call                               },

	//  |0 0 0|0 1 0 0 1|0|
	{   0xff80,     0x0900,     &Sharc::sharcop_relative_jump_compute                       },

	//  |0 0 0|0 1 0 0 1|1|
	{   0xff80,     0x0980,     &Sharc::sharcop_relative_call_compute                       },

	//  |1 1 0|
	{   0xe000,     0xc000,     &Sharc::sharcop_indirect_jump_compute_dreg_dm               },

	//  |1 1 1|
	{   0xe000,     0xe000,     &Sharc::sharcop_relative_jump_compute_dreg_dm               },

	//  |0 0 0|0 1 0 1 0|
	{   0xff00,     0x0a00,     &Sharc::sharcop_rts                                         },

	//  |0 0 0|0 1 0 1 1|
	{   0xff00,     0x0b00,     &Sharc::sharcop_rti                                         },

	//  |0 0 0|0 1 1 0 0|
	{   0xff00,     0x0c00,     &Sharc::sharcop_do_until_counter_imm                        },

	//  |0 0 0|0 1 1 0 1|
	{   0xff00,     0x0d00,     &Sharc::sharcop_do_until_counter_ureg                       },

	//  |0 0 0|0 1 1 1 0|
	{   0xff00,     0x0e00,     &Sharc::sharcop_do_until                                    },

	//  |0 0 0|1 0 0|0|0|
	{   0xff00,     0x1000,     &Sharc::sharcop_dm_to_ureg_direct                           },

	//  |0 0 0|1 0 0|0|1|
	{   0xff00,     0x1100,     &Sharc::sharcop_ureg_to_dm_direct                           },

	//  |0 0 0|1 0 0|1|0|
	{   0xff00,     0x1200,     &Sharc::sharcop_pm_to_ureg_direct                           },

	//  |0 0 0|1 0 0|1|1|
	{   0xff00,     0x1300,     &Sharc::sharcop_ureg_to_pm_direct                           },

	//  |1 0 1|0|x x x|0|
	{   0xf100,     0xa000,     &Sharc::sharcop_dm_to_ureg_indirect                         },

	//  |1 0 1|0|x x x|1|
	{   0xf100,     0xa100,     &Sharc::sharcop_ureg_to_dm_indirect                         },

	//  |1 0 1|1|x x x|0|
	{   0xf100,     0xb000,     &Sharc::sharcop_pm_to_ureg_indirect                         },

	//  |1 0 1|1|x x x|1|
	{   0xf100,     0xb100,     &Sharc::sharcop_ureg_to_pm_indirect                         },

	//  |1 0 0|1|
	{   0xf000,     0x9000,     &Sharc::sharcop_imm_to_dmpm                                 },

	//  |0 0 0|0 1 1 1 1|
	{   0xff00,     0x0f00,     &Sharc::sharcop_imm_to_ureg                                 },

	//  |0 0 0|1 0 1 0 0|
	{   0xff00,     0x1400,     &Sharc::sharcop_sysreg_bitop                                },

	//  |0 0 0|1 0 1 1 0|0|
	{   0xff80,     0x1600,     &Sharc::sharcop_modify                                      },

	//  |0 0 0|1 0 1 1 0|1|
	{   0xff80,     0x1680,     &Sharc::sharcop_bit_reverse                                 },

	//  |0 0 0|1 0 1 1 1|
	{   0xff00,     0x1700,     &Sharc::sharcop_push_pop_stacks                             },

	//  |0 0 0|0 0 0 0 0|0|
	{   0xff80,     0x0000,     &Sharc::sharcop_nop                                         },

	//  |0 0 0|0 0 0 0 0|1|
	{   0xff80,     0x0080,     &Sharc::sharcop_idle                                        },
};


const size_t Sharc::s_num_ops = std::size(s_sharc_opcode_table);
