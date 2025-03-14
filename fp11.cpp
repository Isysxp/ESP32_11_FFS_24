/* pdp11_fp.c: PDP-11 floating point simulator (32b version)

   Copyright (c) 1993-2023, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   05-Jun-23    RMS     Fixed bug in FIS dirty zero check ("Joonio")
   10-Dec-22    RMS     Fixed bug in FUIV operation (James Fehlinger)
   21-Aug-22    RMS     Restored MMR1 operation for 11/44, 11/45-70 (Walter Mueller)
   28-May-18    RMS     Fixed FPCHG macro to avoid undefined operation (Mark Pizzolato)
   24-Mar-15    RMS     MMR1 does not track register changes (Johnny Billquist)
   20-Apr-13    RMS     MMR1 does not track PC changes (Johnny Billquist)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   04-Oct-04    RMS     Added FIS instructions
   19-Jan-03    RMS     Changed mode definitions for Apple Dev Kit conflict
   08-Oct-02    RMS     Fixed macro definitions
   05-Jun-98    RMS     Fixed implementation specific shift bugs
   20-Apr-98    RMS     Fixed bug in MODf integer truncation
   17-Apr-98    RMS     Fixed bug in STCfi range check
   16-Apr-98    RMS     Fixed bugs in STEXP, STCfi, round/pack
   09-Apr-98    RMS     Fixed bug in LDEXP
   04-Apr-98    RMS     Fixed bug in MODf condition codes

   This module simulates the PDP-11 floating point unit (FP11 series).
   It is called from the instruction decoder for opcodes 170000:177777.

   The floating point unit recognizes three instruction formats:

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    no operand
   | 1  1  1  1| 0  0  0  0  0  0|      opcode     |    170000:
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    170077

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    one operand
   | 1  1  1  1| 0  0  0| opcode |    dest spec    |    170100:
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    170777

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    register + operand
   | 1  1  1  1|   opcode  | fac |    dest spec    |    171000:
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    177777

   The instruction space is further extended through use of the floating
   point status register (FPS) mode bits.  Three mode bits affect how
   instructions are interpreted:

        FPS_D           if 0, floating registers are single precision
                        if 1, floating registers are double precision

        FPS_L           if 0, integer operands are word
                        if 1, integer operands are longword

        FPS_T           if 0, floating operations are rounded
                        if 1, floating operations are truncated

   FPS also contains the condition codes for the floating point unit,
   and exception enable bits for individual error conditions.  Exceptions
   cause a trap through 0244, unless the individual exception, or all
   exceptions, are disabled.  Illegal address mode, undefined variable,
   and divide by zero NOP the current instruction; all other exceptions
   permit the instruction to complete.

   Floating point specifiers are similar to integer specifiers, with
   the length of the operand being up to 8 bytes.  In two specific cases,
   the floating point unit reads or writes only two bytes, rather than
   the length specified by the operand type:

        register        for integers, only 16b are accessed; if the
                        operand is 32b, these are the high order 16b
                        of the operand

        immediate       for integers or floating point, only 16b are
                        accessed;  if the operand is 32b or 64b, these
                        are the high order 16b of the operand.

   The J11 cannot update MMR1 on specifier changes, because the
   quantity field is too narrow for +8 or -8. However, the 11/44 and
   11/70 can. So the simulator treats the two cases differently.
   On the J11, the simulator records changes to be made and only
   commits them at instruction. On all other systems, changes occur
   as they happen and are recorded in MMR1. However, all systems
   update the general registers on floating point exceptions. Thus,
   when an exception occurs, the simulator in most cases cannot
   abort but must let the instruction "run to completion." For
   undefined variable and divide by zero, this means skipping
   the actual processing logic.


*/

#include "avr11.h"
#include "kb11.h"

extern KB11 cpu;

/* Floating point status register */

#define FPS_ER          (1u << FPS_V_ER)                /* error */
#define FPS_ID          (1u << FPS_V_ID)                /* interrupt disable */
#define FPS_IUV         (1u << FPS_V_IUV)               /* int on undef var */
#define FPS_IU          (1u << FPS_V_IU)                /* int on underflow */
#define FPS_IV          (1u << FPS_V_IV)                /* int on overflow */
#define FPS_IC          (1u << FPS_V_IC)                /* int on conv error */
#define FPS_D           (1u << FPS_V_D)                 /* single/double */
#define FPS_L           (1u << FPS_V_L)                 /* word/long */
#define FPS_T           (1u << FPS_V_T)                 /* round/truncate */
#define FPS_N           (1u << FPS_V_N)
#define FPS_Z           (1u << FPS_V_Z)
#define FPS_V           (1u << FPS_V_V)
#define FPS_C           (1u << FPS_V_C)
#define FPS_CC          (FPS_N + FPS_Z + FPS_V + FPS_C)
#define FPS_RW          (FPS_ER + FPS_ID + FPS_IUV + FPS_IU + FPS_IV + \
                         FPS_IC + FPS_D + FPS_L + FPS_T + FPS_CC)

/* Floating point exception codes */

#define FEC_OP          2                               /* illegal op/mode */
#define FEC_DZRO        4                               /* divide by zero */
#define FEC_ICVT        6                               /* conversion error */
#define FEC_OVFLO       8                               /* overflow */
#define FEC_UNFLO       10                              /* underflow */
#define FEC_UNDFV       12                              /* undef variable */

/* Floating point format, all assignments 32b relative */

#define FP_V_SIGN       (63 - 32)                       /* high lw: sign */
#define FP_V_EXP        (55 - 32)                       /* exponent */
#define FP_V_HB         FP_V_EXP                        /* hidden bit */
#define FP_V_F0         (48 - 32)                       /* fraction 0 */
#define FP_V_F1         (32 - 32)                       /* fraction 1 */
#define FP_V_FROUND     (31 - 32)                       /* f round point */
#define FP_V_F2         16                              /* low lw: fraction 2 */
#define FP_V_F3         0                               /* fraction 3 */
#define FP_V_DROUND     (-1)                            /* d round point */
#define FP_M_EXP        0377
#define FP_SIGN         (1u << FP_V_SIGN)
#define FP_EXP          (FP_M_EXP << FP_V_EXP)
#define FP_HB           (1u << FP_V_HB)
#define FP_FRACH        ((1u << FP_V_HB) - 1)
#define FP_FRACL        0xFFFFFFFF
#define FP_BIAS         0200                            /* exponent bias */
#define FP_GUARD        3                               /* guard bits */

/* Data lengths */

#define WORD            2
#define LONG            4
#define QUAD            8

/* Reg change word */

#define FPCHG(v,r)      ((int32)((((uint32)(v)) << FPCHG_V_VAL) | (r)))
#define FPCHG_REG       07                              /* register number */
#define FPCHG_V_VAL     3                               /* offset to value */
#define FPCHG_GETREG(x) ((x) & FPCHG_REG)
#define FPCHG_GETVAL(x) ((x) >> FPCHG_V_VAL)

/* Double precision operations on 64b quantities */

#define F_LOAD(qd,ac,ds) \
                        ds.h = ac.h; ds.l = (qd)? ac.l: 0
#define F_LOAD_P(qd,ac,ds) \
                        ds->h = ac.h; ds->l = (qd)? ac.l: 0
#define F_LOAD_FRAC(qd,ac,ds) \
                        ds.h = (ac.h & FP_FRACH) | FP_HB; \
                        ds.l = (qd)? ac.l: 0
#define F_STORE(qd,sr,ac) \
                        ac.h = sr.h; if ((qd)) ac.l = sr.l
#define F_STORE_P(qd,sr,ac) \
                        ac.h = sr->h; if ((qd)) ac.l = sr->l
#define F_GET_FRAC_P(sr,ds) \
                        ds.l = sr->l; \
                        ds.h = (sr->h & FP_FRACH) | FP_HB
#define F_ADD(s2,s1,ds) \
                        ds.l = (s1.l + s2.l) & 0xFFFFFFFF; \
                        ds.h = (s1.h + s2.h + (ds.l < s2.l)) & 0xFFFFFFFF
#define F_SUB(s2,s1,ds) \
                        ds.h = (s1.h - s2.h - (s1.l < s2.l)) & 0xFFFFFFFF; \
                        ds.l = (s1.l - s2.l) & 0xFFFFFFFF
#define F_LT(x,y)       ((x.h < y.h) || ((x.h == y.h) && (x.l < y.l)))
#define F_LT_AP(x,y)    (((x->h & ~FP_SIGN) < (y->h & ~FP_SIGN)) || \
                        (((x->h & ~FP_SIGN) == (y->h & ~FP_SIGN)) && (x->l < y->l)))
#define F_LSH_V(sr,n,ds) \
                        ds.h = (((n) >= 32)? (sr.l << ((n) - 32)): \
                                (sr.h << (n)) | ((sr.l >> (32 - (n))) & and_mask[n])) \
                                & 0xFFFFFFFF; \
                        ds.l = ((n) >= 32)? 0: (sr.l << (n)) & 0xFFFFFFFF
#define F_RSH_V(sr,n,ds) \
                        ds.l = (((n) >= 32)? (sr.h >> ((n) - 32)) & and_mask[64 - (n)]: \
                                ((sr.l >> (n)) & and_mask[32 - (n)]) | \
                                (sr.h << (32 - (n)))) & 0xFFFFFFFF; \
                        ds.h = ((n) >= 32)? 0: \
                                ((sr.h >> (n)) & and_mask[32 - (n)]) & 0xFFFFFFFF

/* For the constant shift macro, arguments must in the range [2,31] */

#define F_LSH_1(ds)     ds.h = ((ds.h << 1) | ((ds.l >> 31) & 1)) & 0xFFFFFFFF; \
                        ds.l = (ds.l << 1) & 0xFFFFFFFF
#define F_RSH_1(ds)     ds.l = ((ds.l >> 1) & 0x7FFFFFFF) | ((ds.h & 1) << 31); \
                        ds.h = ((ds.h >> 1) & 0x7FFFFFFF)
#define F_LSH_K(sr,n,ds) \
                        ds.h = ((sr.h << (n)) | ((sr.l >> (32 - (n))) & and_mask[n])) \
                                & 0xFFFFFFFF; \
                        ds.l = (sr.l << (n)) & 0xFFFFFFFF
#define F_RSH_K(sr,n,ds) \
                        ds.l = (((sr.l >> (n)) & and_mask[32 - (n)]) | \
                                (sr.h << (32 - (n)))) & 0xFFFFFFFF; \
                        ds.h = ((sr.h >> (n)) & and_mask[32 - (n)]) & 0xFFFFFFFF
#define F_LSH_GUARD(ds) F_LSH_K(ds,FP_GUARD,ds)
#define F_RSH_GUARD(ds) F_RSH_K(ds,FP_GUARD,ds)

#define GET_BIT(ir,n)   (((ir) >> (n)) & 1)
#define GET_SIGN(ir)    GET_BIT((ir), FP_V_SIGN)
#define GET_EXP(ir)     (((ir) >> FP_V_EXP) & FP_M_EXP)
#define GET_SIGN_L(ir)  GET_BIT((ir), 31)
#define GET_SIGN_W(ir)  GET_BIT((ir), 15)

int32 STKLIM = 0;
fpac_t FR[6] = { {0} };                                 /* fp accumulators */
int32 cm = 0;                                           /* *** Current mode ***/

static int32 FEC, FEA, FPS;
static int32 N, Z, V, C;
static uint16_t* R = cpu.R;
int32 dsenable = 0, isenable = 0;

fpac_t zero_fac = { 0, 0 };
fpac_t one_fac = { 1, 0 };
fpac_t fround_fac = { (1u << (FP_V_FROUND + 32)), 0 };
fpac_t fround_guard_fac = { 0, (1u << (FP_V_FROUND + FP_GUARD)) };
fpac_t dround_guard_fac = { (1u << (FP_V_DROUND + FP_GUARD)), 0 };
fpac_t fmask_fac = { 0xFFFFFFFF, (1u << (FP_V_HB + FP_GUARD + 1)) - 1 };
static const uint32 and_mask[33] = { 0,
    0x1, 0x3, 0x7, 0xF,
    0x1F, 0x3F, 0x7F, 0xFF,
    0x1FF, 0x3FF, 0x7FF, 0xFFF,
    0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF,
    0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF,
    0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF,
    0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF,
    0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
};
int32 backup_PC;
int32 fp_change;
int ftrap = 0;

t_bool fpnotrap(int32 code);
int32 GeteaFW(int32 spec);
int32 GeteaFP(int32 spec, int32 len);
void fp_reg_change(int32 len, int32 reg);
uint32 ReadI(int32 addr, int32 spec, int32 len);
t_bool ReadFP(fpac_t* fac, int32 addr, int32 spec, int32 len);
void WriteI(int32 data, int32 addr, int32 spec, int32 len);
void WriteFP(fpac_t* data, int32 addr, int32 spec, int32 len);
int32 setfcc(int32 old_status, int32 result_high, int32 newV);
int32 addfp11(fpac_t* src1, fpac_t* src2);
int32 mulfp11(fpac_t* src1, fpac_t* src2);
int32 divfp11(fpac_t* src1, fpac_t* src2);
int32 modfp11(fpac_t* src1, fpac_t* src2, fpac_t* frac);
void frac_mulfp11(fpac_t* src1, fpac_t* src2);
int32 roundfp11(fpac_t* src);
int32 round_and_pack(fpac_t* fac, int32 exp, fpac_t* frac, int r);

int32 ReadW(int32 addr)
{
    return (cpu.read16(addr));
}
extern int32 ReadMW(int32 addr)
{
    return(cpu.read16(addr));
}
void WriteW(int32 data, int32 addr)
{
    cpu.write16(addr, data);
}
extern void PWriteW(int32 data, int32 addr)
{
    cpu.write16(addr, data);
}
void set_stack_trap(int32 adr)
{}


/* Set up for instruction decode and execution */

void fp11(int32 IR)
{
    int32 dst, ea, ac, dstspec;
    int32 i, qdouble, lenf, leni;
    int32 newV, exp, sign;
    fpac_t fac, fsrc, modfrac;
    static const uint32 i_limit[2][2] = {
        { 0x80000000, 0x80010000 },
        { 0x80000000, 0x80000001 }
    };

    backup_PC = cpu.PC;                                         /* save PC for FEA */
    fp_change = 0;                                          /* assume no reg chg */
    ac = (IR >> 6) & 03;                                    /* fac is IR<7:6> */
    dstspec = IR & 077;
    qdouble = FPS & FPS_D;
    lenf = qdouble ? QUAD : LONG;
    ftrap = 0;
    switch ((IR >> 8) & 017) {                              /* decode IR<11:8> */

    case 000:
        switch (ac) {                                   /* decode IR<7:6> */

        case 0:                                         /* specials */
            if (IR == 0170000) {                        /* CFCC */
                N = (FPS >> PSW_V_N) & 1;
                Z = (FPS >> PSW_V_Z) & 1;
                V = (FPS >> PSW_V_V) & 1;
                C = (FPS >> PSW_V_C) & 1;
            }
            else if (IR == 0170001)                     /* SETF */
                FPS = FPS & ~FPS_D;
            else if (IR == 0170002)                     /* SETI */
                FPS = FPS & ~FPS_L;
            else if (IR == 0170011)                     /* SETD */
                FPS = FPS | FPS_D;
            else if (IR == 0170012)                     /* SETL */
                FPS = FPS | FPS_L;
            else fpnotrap(FEC_OP);
            break;

        case 1:                                         /* LDFPS */
            dst = (dstspec <= 07) ? R[dstspec] : ReadW(GeteaFW(dstspec));
            FPS = dst & FPS_RW;
            break;

        case 2:                                         /* STFPS */
            FPS = FPS & FPS_RW;
            if (dstspec <= 07)
                R[dstspec] = FPS;
            else WriteW(FPS, GeteaFW(dstspec));
            break;

        case 3:                                         /* STST */
            if (dstspec <= 07)
                R[dstspec] = FEC;
            else WriteI((FEC << 16) | FEA, GeteaFP(dstspec, LONG),
                dstspec, LONG);
            break;
        }                                           /* end switch <7:6> */
        break;                                          /* end case 0 */

    case 001:
        switch (ac) {                                   /* decode IR<7:6> */

        case 0:                                         /* CLRf */
            WriteFP(&zero_fac, GeteaFP(dstspec, lenf), dstspec, lenf);
            FPS = (FPS & ~FPS_CC) | FPS_Z;
            break;

        case 1:                                         /* TSTf */
            if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf))
                FPS = setfcc(FPS, fsrc.h, 0);
            break;

        case 2:                                         /* ABSf */
            if (ReadFP(&fsrc, ea = GeteaFP(dstspec, lenf), dstspec, lenf)) {
                if (GET_EXP(fsrc.h) == 0)
                    fsrc = zero_fac;
                else fsrc.h = fsrc.h & ~FP_SIGN;
                WriteFP(&fsrc, ea, dstspec, lenf);
                FPS = setfcc(FPS, fsrc.h, 0);
            }
            break;

        case 3:                                         /* NEGf */
            if (ReadFP(&fsrc, ea = GeteaFP(dstspec, lenf), dstspec, lenf)) {
                if (GET_EXP(fsrc.h) == 0)
                    fsrc = zero_fac;
                else fsrc.h = fsrc.h ^ FP_SIGN;
                WriteFP(&fsrc, ea, dstspec, lenf);
                FPS = setfcc(FPS, fsrc.h, 0);
            }
            break;
        }                                           /* end switch <7:6> */
        break;                                          /* end case 1 */

    case 005:                                           /* LDf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_STORE(qdouble, fsrc, FR[ac]);
            FPS = setfcc(FPS, fsrc.h, 0);
        }
        break;

    case 010:                                           /* STf */
        F_LOAD(qdouble, FR[ac], fac);
        WriteFP(&fac, GeteaFP(dstspec, lenf), dstspec, lenf);
        break;

    case 017:                                           /* LDCff' */
        if (ReadFP(&fsrc, GeteaFP(dstspec, 12 - lenf), dstspec, 12 - lenf)) {
            if (GET_EXP(fsrc.h) == 0)
                fsrc = zero_fac;
            if ((FPS & (FPS_D + FPS_T)) == 0)
                newV = roundfp11(&fsrc);
            else newV = 0;
            F_STORE(qdouble, fsrc, FR[ac]);
            FPS = setfcc(FPS, fsrc.h, newV);
        }
        break;

    case 014:                                           /* STCff' */
        F_LOAD(qdouble, FR[ac], fac);
        if (GET_EXP(fac.h) == 0)
            fac = zero_fac;
        if ((FPS & (FPS_D + FPS_T)) == FPS_D)
            newV = roundfp11(&fac);
        else newV = 0;
        WriteFP(&fac, GeteaFP(dstspec, 12 - lenf), dstspec, 12 - lenf);
        FPS = setfcc(FPS, fac.h, newV);
        break;

    case 007:                                           /* CMPf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            if (GET_EXP(fsrc.h) == 0)
                fsrc = zero_fac;
            if (GET_EXP(fac.h) == 0)
                fac = zero_fac;
            if ((fsrc.h == fac.h) && (fsrc.l == fac.l)) { /* equal? */
                FPS = (FPS & ~FPS_CC) | FPS_Z;
                if ((fsrc.h | fsrc.l) == 0) {           /* zero? */
                    F_STORE(qdouble, zero_fac, FR[ac]);
                }
            }
            else {                                      /* unequal */
                FPS = (FPS & ~FPS_CC) | ((fsrc.h >> (FP_V_SIGN - PSW_V_N)) & FPS_N);
                if ((GET_SIGN(fsrc.h ^ fac.h) == 0) && (fac.h != 0) &&
                    F_LT(fsrc, fac))
                    FPS = FPS ^ FPS_N;
            }
        }
        break;

    case 015:                                           /* LDEXP */
        dst = (dstspec <= 07) ? R[dstspec] : ReadW(GeteaFW(dstspec));
        F_LOAD(qdouble, FR[ac], fac);
        fac.h = (fac.h & ~FP_EXP) | (((dst + FP_BIAS) & FP_M_EXP) << FP_V_EXP);
        newV = 0;
        if ((dst > 0177) && (dst <= 0177600)) {
            if (dst < 0100000) {
                if (fpnotrap(FEC_OVFLO))
                    fac = zero_fac;
                newV = FPS_V;
            }
            else {
                if (fpnotrap(FEC_UNFLO))
                    fac = zero_fac;
            }
        }
        F_STORE(qdouble, fac, FR[ac]);
        FPS = setfcc(FPS, fac.h, newV);
        break;

    case 012:                                           /* STEXP */
        dst = (GET_EXP(FR[ac].h) - FP_BIAS) & 0177777;
        N = GET_SIGN_W(dst);
        Z = (dst == 0);
        V = 0;
        C = 0;
        FPS = (FPS & ~FPS_CC) | (N << PSW_V_N) | (Z << PSW_V_Z);
        if (dstspec <= 07)
            R[dstspec] = dst;
        else WriteW(dst, GeteaFW(dstspec));
        break;

    case 016:                                           /* LDCif */
        leni = FPS & FPS_L ? LONG : WORD;
        if (dstspec <= 07)
            fac.l = R[dstspec] << 16;
        else fac.l = ReadI(GeteaFP(dstspec, leni), dstspec, leni);
        fac.h = 0;
        if (fac.l) {
            if ((sign = GET_SIGN_L(fac.l)))
                fac.l = (fac.l ^ 0xFFFFFFFF) + 1;
            for (i = 0; GET_SIGN_L(fac.l) == 0; i++)
                fac.l = fac.l << 1;
            exp = ((FPS & FPS_L) ? FP_BIAS + 32 : FP_BIAS + 16) - i;
            fac.h = (sign << FP_V_SIGN) | (exp << FP_V_EXP) |
                ((fac.l >> (31 - FP_V_HB)) & FP_FRACH);
            fac.l = (fac.l << (FP_V_HB + 1)) & FP_FRACL;
            if ((FPS & (FPS_D + FPS_T)) == 0)
                roundfp11(&fac);
        }
        F_STORE(qdouble, fac, FR[ac]);
        FPS = setfcc(FPS, fac.h, 0);
        break;

    case 013:                                           /* STCfi */
        sign = GET_SIGN(FR[ac].h);                     /* get sign, */
        exp = GET_EXP(FR[ac].h);                       /* exponent, */
        F_LOAD_FRAC(qdouble, FR[ac], fac);             /* fraction */
        if (FPS & FPS_L) {
            leni = LONG;
            i = FP_BIAS + 32;
        }
        else {
            leni = WORD;
            i = FP_BIAS + 16;
        }
        C = 0;
        if (exp <= FP_BIAS)
            dst = 0;
        else if (exp > i) {
            dst = 0;
            C = 1;
        }
        else {
            F_RSH_V(fac, FP_V_HB + 1 + i - exp, fsrc);
            if (leni == WORD)
                fsrc.l = fsrc.l & ~0177777;
            if (fsrc.l >= i_limit[leni == LONG][sign]) {
                dst = 0;
                C = 1;
            }
            else {
                dst = fsrc.l;
                if (sign)
                    dst = -dst;
            }
        }
        N = GET_SIGN_L(dst);
        Z = (dst == 0);
        V = 0;
        if (C)
            fpnotrap(FEC_ICVT);
        FPS = (FPS & ~FPS_CC) | (N << PSW_V_N) |
            (Z << PSW_V_Z) | (C << PSW_V_C);
        if (dstspec <= 07)
            R[dstspec] = (dst >> 16) & 0177777;
        else WriteI(dst, GeteaFP(dstspec, leni), dstspec, leni);
        break;

    case 002:                                           /* MULf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            newV = mulfp11(&fac, &fsrc);
            F_STORE(qdouble, fac, FR[ac]);
            FPS = setfcc(FPS, fac.h, newV);
        }
        break;

    case 003:                                           /* MODf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            newV = modfp11(&fac, &fsrc, &modfrac);
            F_STORE(qdouble, fac, FR[ac | 1]);
            F_STORE(qdouble, modfrac, FR[ac]);
            FPS = setfcc(FPS, modfrac.h, newV);
        }
        break;

    case 004:                                           /* ADDf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            newV = addfp11(&fac, &fsrc);
            F_STORE(qdouble, fac, FR[ac]);
            FPS = setfcc(FPS, fac.h, newV);
        }
        break;

    case 006:                                           /* SUBf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            if (GET_EXP(fsrc.h) != 0)
                fsrc.h = fsrc.h ^ FP_SIGN;
            newV = addfp11(&fac, &fsrc);
            F_STORE(qdouble, fac, FR[ac]);
            FPS = setfcc(FPS, fac.h, newV);
        }
        break;

    case 011:                                           /* DIVf */
        if (ReadFP(&fsrc, GeteaFP(dstspec, lenf), dstspec, lenf)) {
            F_LOAD(qdouble, FR[ac], fac);
            if (GET_EXP(fsrc.h) == 0)                  /* divide by zero? */
                fpnotrap(FEC_DZRO);
            else {                                      /* no, do divide */
                newV = divfp11(&fac, &fsrc);
                F_STORE(qdouble, fac, FR[ac]);
                FPS = setfcc(FPS, fac.h, newV);
            }
        }
        break;
    }                                               /* end switch fop */

/* Now process any general register modification */

    if (fp_change != 0) {
        int32 reg = FPCHG_GETREG(fp_change);               /* get register */
        int32 val = FPCHG_GETVAL(fp_change);               /* get value */
        if (val & 020)                                      /* negative? */
            val = val | (-16);                              /* ensure proper sext */
        R[reg] = (R[reg] + val) & 0177777;                  /* commit change */
    }
    cpu.PSW = (cpu.PSW & (0xfff0)) | (N << 3) | (Z << 2) | (V << 1) | C;
    if (ftrap)
        trap(INTFPP);
    return;
}

/* Effective address calculation for word integers */

int32 GeteaFW(int32 spec)
{
    int32 adr, reg, ds;

    reg = spec & 07;                                        /* register number */
    ds = (reg == 7) ? isenable : dsenable;                    /* dspace if not PC */
    switch (spec >> 3) {                                    /* decode spec<5:3> */

    default:                                            /* can't get here */
    case 1:                                             /* (R) */
        return (R[reg] | ds);

    case 2:                                             /* (R)+ */
        adr = R[reg];                                   /* post increment */
        fp_reg_change(2, reg);                         /* update */
        return (adr | ds);

    case 3:                                             /* @(R)+ */
        adr = R[reg];                                   /* post increment */
        fp_reg_change(2, reg);                         /* update */
        adr = ReadW(adr | ds);
        return (adr | dsenable);

    case 4:                                             /* -(R) */
        adr = (R[reg] - 2) & 0177777;                   /* predecrement */
        fp_reg_change(-2, reg);                        /* update */
        if ((reg == 6) && (cm == MD_KER) && (adr < (STKLIM + STKL_Y)))
            set_stack_trap(adr);
        return (adr | ds);

    case 5:                                             /* @-(R) */
        adr = (R[reg] - 2) & 0177777;                   /* predecrement */
        fp_reg_change(-2, reg);                        /* update */
        if ((reg == 6) && (cm == MD_KER) && (adr < (STKLIM + STKL_Y)))
            set_stack_trap(adr);
        adr = ReadW(adr | ds);
        return (adr | dsenable);

    case 6:                                             /* d(r) */
        adr = ReadW(cpu.R[7] | isenable);
        cpu.R[7] = (cpu.R[7] + 2) & 0177777;
        return (((R[reg] + adr) & 0177777) | dsenable);

    case 7:                                             /* @d(R) */
        adr = ReadW(cpu.R[7] | isenable);
        cpu.R[7] = (cpu.R[7] + 2) & 0177777;
        adr = ReadW(((R[reg] + adr) & 0177777) | dsenable);
        return (adr | dsenable);
    }                                               /* end switch */
}

/* Effective address calculation for fp operands

   Inputs:
        spec    =       specifier
        len     =       length
   Outputs:
        VA      =       virtual address

   Warnings:
        - Do not call this routine for integer mode 0 operands
        - Do not call this routine more than once per instruction

   Note that for modes 06 and 07, it is OKAY to bail out of the FP
   instruction immediately; no general register updates can occur.
*/

int32 GeteaFP(int32 spec, int32 len)
{
    int32 adr, reg, ds;

    reg = spec & 07;                                        /* reg number */
    ds = (reg == 7) ? isenable : dsenable;                    /* dspace if not PC */
    switch (spec >> 3) {                                    /* case on spec */

    case 0:                                             /* floating AC */
        if (reg >= 06) {
            fpnotrap(FEC_OP);
            //            ABORT(TRAP_INT);                           /* scuttle instr */
            trap(INTFPP);
        }
        return 0;

    case 1:                                             /* (R) */
        return (R[reg] | ds);

    case 2:                                             /* (R)+ */
        adr = R[reg];                                   /* post increment */
        if (reg == 7)                                   /* # is always length 2 */
            len = 2;
        fp_reg_change(len, reg);                       /* update */
        return (adr | ds);

    case 3:                                             /* @(R)+ */
        adr = R[reg];                                   /* post increment */
        fp_reg_change(2, reg);                         /* update */
        adr = ReadW(adr | ds);
        return (adr | dsenable);

    case 4:                                             /* -(R) */
        adr = (R[reg] - len) & 0177777;                 /* predecrement */
        fp_reg_change(-len, reg);                      /* update */
        if ((reg == 6) && (cm == MD_KER) && (adr < (STKLIM + STKL_Y)))
            set_stack_trap(adr);
        return (adr | ds);

    case 5:                                             /* @-(R) */
        adr = (R[reg] - 2) & 0177777;                   /* predecrement */
        fp_reg_change(-2, reg);                        /* update */
        if ((reg == 6) && (cm == MD_KER) && ((adr - 2) < (STKLIM + STKL_Y)))
            set_stack_trap(adr);
        adr = ReadW(adr | ds);
        return (adr | dsenable);

    case 6:                                             /* d(r) */
        adr = ReadW(cpu.R[7] | isenable);
        cpu.R[7] = (cpu.R[7] + 2) & 0177777;
        return (((R[reg] + adr) & 0177777) | dsenable);

    case 7:                                             /* @d(R) */
        adr = ReadW(cpu.R[7] | isenable);
        cpu.R[7] = (cpu.R[7] + 2) & 0177777;
        adr = ReadW(((R[reg] + adr) & 0177777) | dsenable);
        return (adr | dsenable);
    }                                               /* end switch */

    return 0;
}

/* Specifier register change

   On systems with full memory management, the 11/44, 11/45, and 11/70
   operate differently than J11. The former do normal register modification
   and track changes in MMR1; on an abort, the register modifications
   are visible. The J11 does not perform normal register modification
   and tracking. Instead, it tracks changes internally and only updates
   the general registers upon successful completion of the instruction.
   On an abort, the general registers are unchanged.

   This routine performs the appropriate bookkeeping for the different
   models.
*/

void fp_reg_change(int32 len, int32 reg)
{
    R[reg] = (R[reg] + len) & 0177777;                  /* Commit immediately */
    return;
    // MMR1 set in primary cpu loop.
    //if (CPUT(CPUT_J)) {                                /* J11? */
    //    if (reg == 7)
    //        R[reg] = (R[reg] + len) & 0177777;          /* commit PC changes now */
    //    else fp_change = FPCHG(len, reg);              /* track other changes */
    //}
    //else {                                              /* all others */
    //    R[reg] = (R[reg] + len) & 0177777;              /* commit reg changes */
    //    if (update_MM)                                  /* if not frozen */
    //        MMR1 = ((len & 037) << 3) | reg;             /* update MMR1 */
    //}
    //return;
}

/* Read integer operand

   Inputs:
        VA      =       virtual address, VA<18:16> = mode, I/D space
        spec    =       specifier
        len     =       length (2/4 bytes)
   Outputs:
        data    =       data read from memory or I/O space
*/

uint32 ReadI(int32 VA, int32 spec, int32 len)
{
    if ((len == WORD) || (spec == 027))
        return (ReadW(VA) << 16);
    return ((ReadW(VA) << 16) |
        ReadW((VA & ~0177777) | ((VA + 2) & 0177777)));
}

/* Read floating operand

   Inputs:
        fptr    =       pointer to output
        VA      =       virtual address, VA<18:16> = mode, I/D space
        spec    =       specifier
        len     =       length (4/8 bytes)
   Output:
        TRUE if read succeeded
        FALSE if instruction must be NOP'd
*/

t_bool ReadFP(fpac_t* fptr, int32 VA, int32 spec, int32 len)
{

    int32 exta;

    if (spec <= 07) {
        F_LOAD_P(len == QUAD, FR[spec], fptr);
        return true;
    }
    if (spec == 027) {
        fptr->h = (ReadW(VA) << FP_V_F0);
        fptr->l = 0;
    }
    else {
        exta = VA & ~0177777;
        fptr->h = (ReadW(VA) << FP_V_F0) |
            (ReadW(exta | ((VA + 2) & 0177777)) << FP_V_F1);
        if (len == QUAD) fptr->l =
            (ReadW(exta | ((VA + 4) & 0177777)) << FP_V_F2) |
            (ReadW(exta | ((VA + 6) & 0177777)) << FP_V_F3);
        else fptr->l = 0;
    }
    if ((GET_SIGN(fptr->h) != 0) &&                    /* undef variable? */
        (GET_EXP(fptr->h) == 0) &&
        !fpnotrap(FEC_UNDFV)) {                        /* trap enabled? */
        return false;                               /* NOP instruction */
    }
    return true;
}

/* Write integer result

   Inputs:
        data    =       data to be written
        VA      =       virtual address, VA<18:16> = mode, I/D space
        spec    =       specifier
        len     =       length
   Outputs: none
*/

void WriteI(int32 data, int32 VA, int32 spec, int32 len)
{
    if ((len == WORD) || (spec == 027)) {
        WriteW((data >> 16) & 0177777, VA);
    return;
    }
    PWriteW((data >> 16) & 0177777, VA);
    PWriteW(data & 0177777, VA + 2);
}

/* Write floating result

   Inputs:
        fptr    =       pointer to data to be written
        VA      =       virtual address, VA<18:16> = mode, I/D space
        spec    =       specifier
        len     =       length
   Outputs: none
*/

void WriteFP(fpac_t* fptr, int32 VA, int32 spec, int32 len)
{


    int32 exta, pa, pa2, pa3, pa4;

    if (spec <= 07) {
        F_STORE_P(len == QUAD, fptr, FR[spec]);
        return;
    }

    if (spec <= 07) {
        F_STORE_P(len == QUAD, fptr, FR[spec]);
        return;
    }
    if (spec == 027) {
        WriteW((fptr->h >> FP_V_F0) & 0177777, VA);
        return;
    }

    /* Check all word addresses for breakpoints, and only then
       do the writes.  */

    exta = VA & ~0177777;
    pa = VA;                                      /* relocate */
    pa2 = exta | ((VA + 2) & 0177777);
    if (len == QUAD) {
        pa3 = (exta | ((VA + 4) & 0177777));
        pa4 = (exta | ((VA + 6) & 0177777));
    }

    PWriteW((fptr->h >> FP_V_F0) & 0177777, pa);
    PWriteW((fptr->h >> FP_V_F1) & 0177777, pa2);
    if (len == LONG)
        return;
    PWriteW((fptr->l >> FP_V_F2) & 0177777, pa3);
    PWriteW((fptr->l >> FP_V_F3) & 0177777, pa4);
    return;
}


/* Floating point add

   Inputs:
        facp    =       pointer to src1 (output)
        fsrcp   =       pointer to src2
   Outputs:
        ovflo   =       overflow variable
*/

int32 addfp11(fpac_t* facp, fpac_t* fsrcp)
{
    int32 facexp, fsrcexp, ediff;
    fpac_t facfrac, fsrcfrac;

    if (F_LT_AP(facp, fsrcp)) {                            /* if !fac! < !fsrc! */
        facfrac = *facp;
        *facp = *fsrcp;                                     /* swap operands */
        *fsrcp = facfrac;
    }
    facexp = GET_EXP(facp->h);                             /* get exponents */
    fsrcexp = GET_EXP(fsrcp->h);
    if (facexp == 0) {                                      /* fac = 0? */
        *facp = fsrcexp ? *fsrcp : zero_fac;                  /* result fsrc or 0 */
        return 0;
    }
    if (fsrcexp == 0)                                       /* fsrc = 0? no op */
        return 0;
    ediff = facexp - fsrcexp;                               /* exponent diff */
    if (ediff >= 60)                                        /* too big? no op */
        return 0;
    F_GET_FRAC_P(facp, facfrac);                           /* get fractions */
    F_GET_FRAC_P(fsrcp, fsrcfrac);
    F_LSH_GUARD(facfrac);                                  /* guard fractions */
    F_LSH_GUARD(fsrcfrac);
    if (GET_SIGN(facp->h) != GET_SIGN(fsrcp->h)) {        /* signs different? */
        if (ediff) {                                        /* sub, shf fsrc */
            F_RSH_V(fsrcfrac, ediff, fsrcfrac);
        }
        F_SUB(fsrcfrac, facfrac, facfrac);                 /* sub fsrc from fac */
        if ((facfrac.h | facfrac.l) == 0) {                /* result zero? */
            *facp = zero_fac;                               /* no overflow */
            return 0;
        }
        if (ediff <= 1) {                                   /* big normalize? */
            if ((facfrac.h & (0x00FFFFFF << FP_GUARD)) == 0) {
                F_LSH_K(facfrac, 24, facfrac);
                facexp = facexp - 24;
            }
            if ((facfrac.h & (0x00FFF000 << FP_GUARD)) == 0) {
                F_LSH_K(facfrac, 12, facfrac);
                facexp = facexp - 12;
            }
            if ((facfrac.h & (0x00FC0000 << FP_GUARD)) == 0) {
                F_LSH_K(facfrac, 6, facfrac);
                facexp = facexp - 6;
            }
        }
        while (GET_BIT(facfrac.h, FP_V_HB + FP_GUARD) == 0) {
            F_LSH_1(facfrac);
            facexp = facexp - 1;
        }
    }
    else {
        if (ediff) {
            F_RSH_V(fsrcfrac, ediff, fsrcfrac);            /* add, shf fsrc */
        }
        F_ADD(fsrcfrac, facfrac, facfrac);                 /* add fsrc to fac */
        if (GET_BIT(facfrac.h, FP_V_HB + FP_GUARD + 1)) {
            F_RSH_1(facfrac);                              /* carry out, shift */
            facexp = facexp + 1;
        }
    }
    return round_and_pack(facp, facexp, &facfrac, 1);
}

/* Floating point multiply

   Inputs:
        facp    =       pointer to src1 (output)
        fsrcp   =       pointer to src2
   Outputs:
        ovflo   =       overflow indicator
*/

int32 mulfp11(fpac_t* facp, fpac_t* fsrcp)
{
    int32 facexp, fsrcexp;
    fpac_t facfrac, fsrcfrac;

    facexp = GET_EXP(facp->h);                             /* get exponents */
    fsrcexp = GET_EXP(fsrcp->h);
    if ((facexp == 0) || (fsrcexp == 0)) {                  /* test for zero */
        *facp = zero_fac;
        return 0;
    }
    F_GET_FRAC_P(facp, facfrac);                           /* get fractions */
    F_GET_FRAC_P(fsrcp, fsrcfrac);
    facexp = facexp + fsrcexp - FP_BIAS;                    /* calculate exp */
    facp->h = facp->h ^ fsrcp->h;                          /* calculate sign */
    frac_mulfp11(&facfrac, &fsrcfrac);                     /* multiply fracs */

    /* Multiplying two numbers in the range [.5,1) produces a result in the
       range [.25,1).  Therefore, at most one bit of normalization is required
       to bring the result back to the range [.5,1).
    */

    if (GET_BIT(facfrac.h, FP_V_HB + FP_GUARD) == 0) {
        F_LSH_1(facfrac);
        facexp = facexp - 1;
    }
    return round_and_pack(facp, facexp, &facfrac, 1);
}

/* Floating point mod

   Inputs:
        facp    =       pointer to src1 (integer result)
        fsrcp   =       pointer to src2
        fracp   =       pointer to fractional result
   Outputs:
        ovflo   =       overflow indicator

   See notes on multiply for initial operation
*/

int32 modfp11(fpac_t* facp, fpac_t* fsrcp, fpac_t* fracp)
{
    int32 facexp, fsrcexp;
    fpac_t facfrac, fsrcfrac, fmask;

    facexp = GET_EXP(facp->h);                             /* get exponents */
    fsrcexp = GET_EXP(fsrcp->h);
    if ((facexp == 0) || (fsrcexp == 0)) {                  /* test for zero */
        *fracp = zero_fac;
        *facp = zero_fac;
        return 0;
    }
    F_GET_FRAC_P(facp, facfrac);                           /* get fractions */
    F_GET_FRAC_P(fsrcp, fsrcfrac);
    facexp = facexp + fsrcexp - FP_BIAS;                    /* calculate exp */
    fracp->h = facp->h = facp->h ^ fsrcp->h;                /* calculate sign */
    frac_mulfp11(&facfrac, &fsrcfrac);                     /* multiply fracs */

    /* Multiplying two numbers in the range [.5,1) produces a result in the
       range [.25,1).  Therefore, at most one bit of normalization is required
       to bring the result back to the range [.5,1).
    */

    if (GET_BIT(facfrac.h, FP_V_HB + FP_GUARD) == 0) {
        F_LSH_1(facfrac);
        facexp = facexp - 1;
    }

    /* There are three major cases of MODf:

       1. Exp <= FP_BIAS (all fraction).  Return 0 as integer, product as
          fraction.  Underflow can occur.
       2. Exp > FP_BIAS + #fraction bits (all integer).  Return product as
          integer, 0 as fraction.  Overflow can occur.
       3. FP_BIAS < exp <= FP_BIAS + #fraction bits.  Separate integer and
          fraction and return both.  Neither overflow nor underflow can occur.
    */

    if (facexp <= FP_BIAS) {                                /* case 1 */
        *facp = zero_fac;
        return round_and_pack(fracp, facexp, &facfrac, 1);
    }
    if (facexp > ((FPS & FPS_D) ? FP_BIAS + 56 : FP_BIAS + 24)) {
        *fracp = zero_fac;                                  /* case 2 */
        return round_and_pack(facp, facexp, &facfrac, 0);
    }
    F_RSH_V(fmask_fac, facexp - FP_BIAS, fmask);           /* shift mask */
    fsrcfrac.l = facfrac.l & fmask.l;                       /* extract fraction */
    fsrcfrac.h = facfrac.h & fmask.h;
    if ((fsrcfrac.h | fsrcfrac.l) == 0)
        *fracp = zero_fac;
    else {
        F_LSH_V(fsrcfrac, facexp - FP_BIAS, fsrcfrac);
        fsrcexp = FP_BIAS;
        if ((fsrcfrac.h & (0x00FFFFFF << FP_GUARD)) == 0) {
            F_LSH_K(fsrcfrac, 24, fsrcfrac);
            fsrcexp = fsrcexp - 24;
        }
        if ((fsrcfrac.h & (0x00FFF000 << FP_GUARD)) == 0) {
            F_LSH_K(fsrcfrac, 12, fsrcfrac);
            fsrcexp = fsrcexp - 12;
        }
        if ((fsrcfrac.h & (0x00FC0000 << FP_GUARD)) == 0) {
            F_LSH_K(fsrcfrac, 6, fsrcfrac);
            fsrcexp = fsrcexp - 6;
        }
        while (GET_BIT(fsrcfrac.h, FP_V_HB + FP_GUARD) == 0) {
            F_LSH_1(fsrcfrac);
            fsrcexp = fsrcexp - 1;
        }
        round_and_pack(fracp, fsrcexp, &fsrcfrac, 1);
    }
    facfrac.l = facfrac.l & ~fmask.l;
    facfrac.h = facfrac.h & ~fmask.h;
    return round_and_pack(facp, facexp, &facfrac, 0);
}

/* Fraction multiply

   Inputs:
        f1p     =       pointer to multiplier (output)
        f2p     =       pointer to multiplicand fraction

   Note: the inputs are unguarded; the output is guarded.

   This routine performs a classic shift-and-add multiply.  The low
   order bit of the multiplier is tested; if 1, the multiplicand is
   added into the high part of the double precision result.  The
   result and the multiplier are both shifted right 1.

   For the 24b x 24b case, this routine develops 48b of result.
   For the 56b x 56b case, this routine only develops the top 64b
   of the the result.  Because the inputs are normalized fractions,
   the interesting part of the result is the high 56+guard bits.
   Everything shifted off to the right, beyond 64b, plays no part
   in rounding or the result.

   There are many possible optimizations in this routine: scanning
   for groups of zeroes, particularly in the 56b x 56b case; using
   "extended multiply" capability if available in the hardware.
*/

void frac_mulfp11(fpac_t* f1p, fpac_t* f2p)
{
    fpac_t result, mpy, mpc;
    int32 i;

    result = zero_fac;                                      /* clear result */
    mpy = *f1p;                                             /* get operands */
    mpc = *f2p;
    F_LSH_GUARD(mpc);                                      /* guard multipicand */
    if ((mpy.l | mpc.l) == 0) {                             /* 24b x 24b? */
        for (i = 0; i < 24; i++) {
            if (mpy.h & 1)
                result.h = result.h + mpc.h;
            F_RSH_1(result);
            mpy.h = mpy.h >> 1;
        }
    }
    else {
        if (mpy.l != 0) {                                   /* 24b x 56b? */
            for (i = 0; i < 32; i++) {
                if (mpy.l & 1) {
                    F_ADD(mpc, result, result);
                }
                F_RSH_1(result);
                mpy.l = mpy.l >> 1;
            }
        }
        for (i = 0; i < 24; i++) {
            if (mpy.h & 1) {
                F_ADD(mpc, result, result);
            }
            F_RSH_1(result);
            mpy.h = mpy.h >> 1;
        }
    }
    *f1p = result;
    return;
}

/* Floating point divide

   Inputs:
        facp    =       pointer to dividend (output)
        fsrcp   =       pointer to divisor
   Outputs:
        ovflo   =       overflow indicator

   Source operand must be checked for zero by caller!
*/

int32 divfp11(fpac_t* facp, fpac_t* fsrcp)
{
    int32 facexp, fsrcexp, i, count, qd;
    fpac_t facfrac, fsrcfrac, quo;

    fsrcexp = GET_EXP(fsrcp->h);                           /* get divisor exp */
    facexp = GET_EXP(facp->h);                             /* get dividend exp */
    if (facexp == 0) {                                      /* test for zero */
        *facp = zero_fac;                                   /* result zero */
        return 0;
    }
    F_GET_FRAC_P(facp, facfrac);                           /* get fractions */
    F_GET_FRAC_P(fsrcp, fsrcfrac);
    F_LSH_GUARD(facfrac);                                  /* guard fractions */
    F_LSH_GUARD(fsrcfrac);
    facexp = facexp - fsrcexp + FP_BIAS + 1;                /* calculate exp */
    facp->h = facp->h ^ fsrcp->h;                           /* calculate sign */
    qd = FPS & FPS_D;
    count = FP_V_HB + FP_GUARD + (qd ? 33 : 1);               /* count = 56b/24b */

    quo = zero_fac;
    for (i = count; (i > 0) && ((facfrac.h | facfrac.l) != 0); i--) {
        F_LSH_1(quo);                                      /* shift quotient */
        if (!F_LT(facfrac, fsrcfrac)) {                    /* divd >= divr? */
            F_SUB(fsrcfrac, facfrac, facfrac);             /* divd - divr */
            if (qd)                                         /* double or single? */
                quo.l = quo.l | 1;
            else quo.h = quo.h | 1;
        }
        F_LSH_1(facfrac);                                  /* shift divd */
    }
    if (i > 0) {                                            /* early exit? */
        F_LSH_V(quo, i, quo);
    }

    /* Dividing two numbers in the range [.5,1) produces a result in the
       range [.5,2).  Therefore, at most one bit of normalization is required
       to bring the result back to the range [.5,1).  The choice of counts
       and quotient bit positions makes this work correctly.
    */

    if (GET_BIT(quo.h, FP_V_HB + FP_GUARD) == 0) {
        F_LSH_1(quo);
        facexp = facexp - 1;
    }
    return round_and_pack(facp, facexp, &quo, 1);
}

/* Update floating condition codes
   Note that FC is only set by STCfi via the integer condition codes

   Inputs:
        oldst   =       current status
        result  =       high result
        newV    =       new V
   Outputs:
        newst   =       new status
*/

int32 setfcc(int32 oldst, int32 result, int32 newV)
{
    oldst = (oldst & ~FPS_CC) | newV;
    if (GET_SIGN(result))
        oldst = oldst | FPS_N;
    if (GET_EXP(result) == 0)
        oldst = oldst | FPS_Z;
    return oldst;
}

/* Round (in place) floating point number to f_floating

   Inputs:
        fptr    =       pointer to floating number
   Outputs:
        ovflow  =       overflow
*/

int32 roundfp11(fpac_t* fptr)
{
    fpac_t outf;

    outf = *fptr;                                           /* get argument */
    F_ADD(fround_fac, outf, outf);                         /* round */
    if (GET_SIGN(outf.h ^ fptr->h)) {                      /* flipped sign? */
        outf.h = (outf.h ^ FP_SIGN) & 0xFFFFFFFF;           /* restore sign */
        if (fpnotrap(FEC_OVFLO))                           /* if no int, clear */
            *fptr = zero_fac;
        else *fptr = outf;                                  /* return rounded */
        return FPS_V;                                       /* overflow */
    }
    *fptr = outf;                                           /* round was ok */
    return 0;                                               /* no overflow */
}

/* Round result of calculation, test overflow, pack

   Input:
        facp    =       pointer to result, sign in place
        exp     =       result exponent, right justified
        fracp   =       pointer to result fraction, right justified with
                        guard bits
        r       =       round (1) or truncate (0)
   Outputs:
        ovflo   =       overflow indicator
*/

int32 round_and_pack(fpac_t* facp, int32 exp, fpac_t* fracp, int r)
{
    fpac_t frac;

    frac = *fracp;                                          /* get fraction */
    if (r && ((FPS & FPS_T) == 0)) {
        if (FPS & FPS_D) {
            F_ADD(dround_guard_fac, frac, frac);
        }
        else {
            F_ADD(fround_guard_fac, frac, frac);
        }
        if (GET_BIT(frac.h, FP_V_HB + FP_GUARD + 1)) {
            F_RSH_1(frac);
            exp = exp + 1;
        }
    }
    F_RSH_GUARD(frac);
    facp->l = frac.l & FP_FRACL;
    facp->h = (facp->h & FP_SIGN) | ((exp & FP_M_EXP) << FP_V_EXP) |
        (frac.h & FP_FRACH);
    if (exp > 0377) {
        if (fpnotrap(FEC_OVFLO))
            *facp = zero_fac;
        return FPS_V;
    }
    if ((exp <= 0) && (fpnotrap(FEC_UNFLO)))
        *facp = zero_fac;
    return 0;
}

/* Process floating point exception

   Inputs:
        code    =       exception code
   Outputs:
        int     =       FALSE if interrupt enabled, TRUE if disabled
*/

t_bool fpnotrap(int32 code)
{
    static const int32 test_code[] = { 0, 0, 0, FPS_IC, FPS_IV, FPS_IU, FPS_IUV };

    if ((code >= FEC_ICVT) &&
        (code <= FEC_UNDFV) &&
        ((FPS & test_code[code >> 1]) == 0))
        return true;
    FPS = FPS | FPS_ER;
    FEC = code;
    FEA = (backup_PC - 2) & 0177777;
    if ((FPS & FPS_ID) == 0)
        ftrap++;
    return false;
}
