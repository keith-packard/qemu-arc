/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 * Contributed by Cupertino Miranda <cmiranda@synopsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */
#include "qemu/osdep.h"
#include "tcg-arc-adapter.h"

/*
 ***************************************
 * Statically inferred return function *
 ***************************************
 */
#if defined (TARGET_ARC32)
  #define arc_tcgv_tl_temp tcgv_i32_temp
#elif defined (TARGET_ARC64)
  #define arc_tcgv_tl_temp tcgv_i64_temp
#else
    #error "Should not happen"
#endif

bool arc_target_has_option(enum target_options option)
{
    /* TODO: Fill with meaningful cases. */
    switch (option) {
    case LL64_OPTION:
        return true;
        break;
    case DIV_REM_OPTION:
        return true;
        break;
    default:
        break;
    }
    return false;
}

bool arc_is_instruction_operand_a_register(const DisasCtxt *ctx, int nop)
{
    assert(nop < ctx->insn.n_ops);
    operand_t operand = ctx->insn.operands[nop];

    return (operand.type & ARC_OPERAND_IR) != 0;
}

void arc_gen_verifyCCFlag(const DisasCtxt *ctx, TCGv ret)
{
    TCGv c1 = tcg_temp_new();

    TCGv nZ = tcg_temp_new();
    TCGv nN = tcg_temp_new();
    TCGv nV = tcg_temp_new();
    TCGv nC = tcg_temp_new();

    switch (ctx->insn.cc) {
    /* AL, RA */
    case ARC_COND_AL:
        tcg_gen_movi_tl(ret, 1);
        break;
    /* EQ, Z */
    case ARC_COND_EQ:
        tcg_gen_mov_tl(ret, cpu_Zf);
        break;
    /* NE, NZ */
    case ARC_COND_NE:
        tcg_gen_xori_tl(ret, cpu_Zf, 1);
        break;
    /* PL, P */
    case ARC_COND_PL:
        tcg_gen_xori_tl(ret, cpu_Nf, 1);
        break;
    /* MI, N: */
    case ARC_COND_MI:
        tcg_gen_mov_tl(ret, cpu_Nf);
        break;
    /* CS, C, LO */
    case ARC_COND_CS:
        tcg_gen_mov_tl(ret, cpu_Cf);
        break;
    /* CC, NC, HS */
    case ARC_COND_CC:
        tcg_gen_xori_tl(ret, cpu_Cf, 1);
        break;
    /* VS, V */
    case ARC_COND_VS:
        tcg_gen_mov_tl(ret, cpu_Vf);
        break;
    /* VC, NV */
    case ARC_COND_VC:
        tcg_gen_xori_tl(ret, cpu_Vf, 1);
        break;
    /* GT */
    case ARC_COND_GT:
        /* (N & V & !Z) | (!N & !V & !Z) === XNOR(N, V) & !Z */
        tcg_gen_eqv_tl(ret, cpu_Nf, cpu_Vf);
        tcg_gen_xori_tl(nZ, cpu_Zf, 1);
        tcg_gen_and_tl(ret, ret, nZ);
        break;
    /* GE */
    case ARC_COND_GE:
        /* (N & V) | (!N & !V)  === XNOR(N, V) */
        tcg_gen_eqv_tl(ret, cpu_Nf, cpu_Vf);
        tcg_gen_andi_tl(ret, ret, 1);
        break;
    /* LT */
    case ARC_COND_LT:
        /* (N & !V) | (!N & V) === XOR(N, V) */
        tcg_gen_xor_tl(ret, cpu_Nf, cpu_Vf);
        break;
    /* LE */
    case ARC_COND_LE:
        /* Z | (N & !V) | (!N & V) === XOR(N, V) | Z */
        tcg_gen_xor_tl(ret, cpu_Nf, cpu_Vf);
        tcg_gen_or_tl(ret, ret, cpu_Zf);
        break;
    /* HI */
    case ARC_COND_HI:
        /* !C & !Z === !(C | Z) */
        tcg_gen_or_tl(ret, cpu_Cf, cpu_Zf);
        tcg_gen_xori_tl(ret, ret, 1);
        break;
    /* LS */
    case ARC_COND_LS:
        /* C | Z */
        tcg_gen_or_tl(ret, cpu_Cf, cpu_Zf);
        break;
    /* PNZ */
    case ARC_COND_PNZ:
        /* !N & !Z === !(N | Z) */
        tcg_gen_or_tl(ret, cpu_Nf, cpu_Zf);
        tcg_gen_xori_tl(ret, ret, 1);
        break;

    default:
        g_assert_not_reached();
    }

    tcg_temp_free(c1);
    tcg_temp_free(nZ);
    tcg_temp_free(nN);
    tcg_temp_free(nV);
    tcg_temp_free(nC);
}

TCGv arc_gen_next_reg(const DisasCtxt *ctx, TCGv reg, bool fail)
{
    ptrdiff_t n = arc_tcgv_tl_temp(reg) - arc_tcgv_tl_temp(cpu_r[0]);
    if (n >= 0 && n < 64) {
        /* Check if REG is an even register. */
        if (n % 2 == 0)
            return cpu_r[n + 1];

        /* REG is an odd register. */
        arc_gen_excp(ctx, EXCP_INST_ERROR, 0, 0);
        return reg;
    }

    /* REG was not a register after all. */
    if (fail)
        g_assert_not_reached();

    return reg;
}

void arc_gen_get_register(TCGv ret, enum arc_registers reg)
{
    switch (reg) {
    case R_SP:
        tcg_gen_mov_tl(ret, cpu_sp);
        break;
    case R_STATUS32:
        gen_helper_get_status32(ret, cpu_env);
        break;
    case R_ACCLO:
        tcg_gen_mov_tl(ret, cpu_acclo);
        break;
    case R_ACCHI:
        tcg_gen_mov_tl(ret, cpu_acchi);
        break;
    default:
        g_assert_not_reached();
    }
}


void arc_gen_set_register(enum arc_registers reg, TCGv value)
{
    switch (reg) {
    case R_SP:
        tcg_gen_mov_tl(cpu_sp, value);
        break;
    case R_STATUS32:
        gen_helper_set_status32(cpu_env, value);
        break;
    case R_ACCLO:
        tcg_gen_mov_tl(cpu_acclo, value);
        break;
    case R_ACCHI:
        tcg_gen_mov_tl(cpu_acchi, value);
        break;
    default:
        g_assert_not_reached();
    }
}

/* TODO: Get this from props ... */
void arc_has_interrupts(const DisasCtxt *ctx, TCGv ret)
{
    tcg_gen_movi_tl(ret, 1);
}


#define MEMIDX (ctx->mem_idx)

#ifdef TARGET_ARC32
const MemOp memop_for_size_sign[2][3] = {
    { MO_UL, MO_UB, MO_UW }, /* non sign-extended */
    { MO_UL, MO_SB, MO_SW } /* sign-extended */
};
#endif

#ifdef TARGET_ARC64
const MemOp memop_for_size_sign[2][4] = {
    { MO_UL, MO_UB, MO_UW, MO_UQ }, /* non sign-extended */
    { MO_SL, MO_SB, MO_SW, MO_SQ } /* sign-extended */
};
#endif


void arc_gen_set_memory(const DisasCtxt *ctx, TCGv vaddr, int size,
        TCGv src, bool sign_extend)
{
#ifdef TARGET_ARC32
    assert(size != 0x3);
#endif

    tcg_gen_qemu_st_tl(src, vaddr, MEMIDX,
                       memop_for_size_sign[sign_extend][size]);
}


void arc_gen_get_memory(const DisasCtxt *ctx, TCGv dest, TCGv vaddr,
        int size, bool sign_extend)
{
#ifdef TARGET_ARC32
    assert(size != 0x3);
#endif

    tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX,
                       memop_for_size_sign[sign_extend][size]);
}

void arc_gen_no_further_loads_pending(const DisasCtxt *ctx, TCGv ret)
{
    /* TODO: To complete on SMP support. */
    tcg_gen_movi_tl(ret, 1);
}

void arc_gen_set_debug(const DisasCtxt *ctx, bool value)
{
    /* TODO: Could not find a reson to set this. */
}