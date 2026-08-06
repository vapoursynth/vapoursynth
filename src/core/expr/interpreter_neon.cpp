/*
* Copyright (c) 2012-2020 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// NEON evaluator for Expr bytecode. compile_jit() only has an x86 backend, so
// without this every std.Expr on aarch64 runs ExprInterpreter::eval() once per
// pixel: a switch over the whole program per pixel, with the register file in
// memory. Evaluating each opcode across a vector instead amortizes the dispatch
// and does the arithmetic four-wide.
//
// Every opcode below mirrors ExprInterpreter::eval() exactly, so the two produce
// identical results. Two things that mirroring depends on, both easy to get
// wrong:
//
//   - The arithmetic opcodes are written as plain C++ operators on the vector
//     type, not as vaddq_f32/vmulq_f32/vfmaq_f32 calls. With the default
//     -ffp-contract=on a compiler fuses the scalar interpreter's
//     `SRC2 * SRC3 + SRC1` into one multiply-add (rounding once) but keeps the
//     explicit negation in `-(SRC2 * SRC3) + SRC1` (rounding twice, and flipping
//     the sign bit of a NaN product). Writing the same expression is what makes
//     the compiler reach the same decision in both; hand-picked intrinsics
//     cannot track it.
//   - std::max(a, b) is `a < b ? b : a` and std::min(a, b) is `b < a ? b : a`,
//     which differ from vmaxq_f32/vminq_f32 for NaN and for max(-0.0, +0.0), so
//     they are written as an explicit compare and select.
//
// The transcendentals have no NEON equivalent and are evaluated per lane through
// the same libm calls, so they cannot drift at all.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <arm_neon.h>
#include "interpreter_neon.h"

namespace expr {
namespace {

constexpr int LANES = 4;

float32x4_t bool2float(uint32x4_t mask)
{
    // mask is all-ones or all-zeros, so masking the bit pattern of 1.0f yields
    // exactly 1.0f or 0.0f.
    return vreinterpretq_f32_u32(vandq_u32(mask, vreinterpretq_u32_f32(vdupq_n_f32(1.0f))));
}

uint32x4_t float2bool(float32x4_t x)
{
    return vcgtq_f32(x, vdupq_n_f32(0.0f));
}

float32x4_t maxnum(float32x4_t a, float32x4_t b)
{
    return vbslq_f32(vcltq_f32(a, b), b, a);
}

float32x4_t minnum(float32x4_t a, float32x4_t b)
{
    return vbslq_f32(vcltq_f32(b, a), b, a);
}

// Matches clamp_int<T>(): min(max(x, lo), hi), then lrint. vcvtnq_s32_f32 rounds
// to nearest with ties to even, which is lrint's behaviour under the default
// rounding mode.
int32x4_t clamp_int(float32x4_t x, float lo, float hi)
{
    return vcvtnq_s32_f32(minnum(maxnum(x, vdupq_n_f32(lo)), vdupq_n_f32(hi)));
}

template <class F>
float32x4_t lanewise(float32x4_t a, F f)
{
    float t[LANES];
    vst1q_f32(t, a);
    for (int i = 0; i < LANES; ++i)
        t[i] = f(t[i]);
    return vld1q_f32(t);
}

template <class F>
float32x4_t lanewise2(float32x4_t a, float32x4_t b, F f)
{
    float ta[LANES], tb[LANES];
    vst1q_f32(ta, a);
    vst1q_f32(tb, b);
    for (int i = 0; i < LANES; ++i)
        ta[i] = f(ta[i], tb[i]);
    return vld1q_f32(ta);
}

} // namespace

NeonInterpreter::NeonInterpreter(const ExprInstruction *bytecode, size_t numInsns) : bytecode(bytecode), numInsns(numInsns)
{
    int maxreg = 0;
    for (size_t i = 0; i < numInsns; ++i) {
        maxreg = std::max(maxreg, bytecode[i].dst);
    }
    registers.resize(static_cast<size_t>(maxreg + 1) * LANES);
}

int NeonInterpreter::pixelsPerIteration()
{
    return LANES;
}

int NeonInterpreter::processRow(const uint8_t * const *srcp, uint8_t *dstp, int w)
{
    float *regs = registers.data();

    int x = 0;
    for (; x + LANES <= w; x += LANES) {
        for (size_t i = 0; i < numInsns; ++i) {
            const ExprInstruction &insn = bytecode[i];

#define SRC1 vld1q_f32(regs + static_cast<size_t>(insn.src1) * LANES)
#define SRC2 vld1q_f32(regs + static_cast<size_t>(insn.src2) * LANES)
#define SRC3 vld1q_f32(regs + static_cast<size_t>(insn.src3) * LANES)
#define DST(v) vst1q_f32(regs + static_cast<size_t>(insn.dst) * LANES, (v))
            switch (insn.op.type) {
            case ExprOpType::MEM_LOAD_U8: {
                uint32_t packed;
                memcpy(&packed, reinterpret_cast<const uint8_t *>(srcp[insn.op.imm.u]) + x, sizeof(packed));
                uint8x8_t bytes = vreinterpret_u8_u32(vdup_n_u32(packed));
                DST(vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(bytes)))));
                break;
            }
            case ExprOpType::MEM_LOAD_U16:
                DST(vcvtq_f32_u32(vmovl_u16(vld1_u16(reinterpret_cast<const uint16_t *>(srcp[insn.op.imm.u]) + x))));
                break;
            case ExprOpType::MEM_LOAD_F16:
                // halfToFloat() is std::bit_cast<_Float16> on AArch64 (see
                // float16_helper.h), i.e. the hardware FCVT that vcvt_f32_f16
                // issues, so this is bit-identical to the scalar interpreter.
                DST(vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(reinterpret_cast<const uint16_t *>(srcp[insn.op.imm.u]) + x))));
                break;
            case ExprOpType::MEM_LOAD_F32:
                DST(vld1q_f32(reinterpret_cast<const float *>(srcp[insn.op.imm.u]) + x));
                break;
            case ExprOpType::CONSTANT: DST(vdupq_n_f32(insn.op.imm.f)); break;
            case ExprOpType::ADD: DST(SRC1 + SRC2); break;
            case ExprOpType::SUB: DST(SRC1 - SRC2); break;
            case ExprOpType::MUL: DST(SRC1 * SRC2); break;
            case ExprOpType::DIV: DST(SRC1 / SRC2); break;
            case ExprOpType::FMA:
                switch (static_cast<FMAType>(insn.op.imm.u)) {
                case FMAType::FMADD: DST(SRC2 * SRC3 + SRC1); break;
                case FMAType::FMSUB: DST(SRC2 * SRC3 - SRC1); break;
                case FMAType::FNMADD: DST(-(SRC2 * SRC3) + SRC1); break;
                case FMAType::FNMSUB: DST(-(SRC2 * SRC3) - SRC1); break;
                };
                break;
            case ExprOpType::MAX: DST(maxnum(SRC1, SRC2)); break;
            case ExprOpType::MIN: DST(minnum(SRC1, SRC2)); break;
            case ExprOpType::EXP: DST(lanewise(SRC1, [](float v) { return std::exp(v); })); break;
            case ExprOpType::LOG: DST(lanewise(SRC1, [](float v) { return std::log(v); })); break;
            case ExprOpType::POW: DST(lanewise2(SRC1, SRC2, [](float a, float b) { return std::pow(a, b); })); break;
            case ExprOpType::SQRT: DST(vsqrtq_f32(SRC1)); break;
            case ExprOpType::SIN: DST(lanewise(SRC1, [](float v) { return std::sin(v); })); break;
            case ExprOpType::COS: DST(lanewise(SRC1, [](float v) { return std::cos(v); })); break;
            case ExprOpType::ABS: DST(vabsq_f32(SRC1)); break;
            case ExprOpType::NEG: DST(-SRC1); break;
            case ExprOpType::CMP:
                switch (static_cast<ComparisonType>(insn.op.imm.u)) {
                case ComparisonType::EQ: DST(bool2float(vceqq_f32(SRC1, SRC2))); break;
                case ComparisonType::LT: DST(bool2float(vcltq_f32(SRC1, SRC2))); break;
                case ComparisonType::LE: DST(bool2float(vcleq_f32(SRC1, SRC2))); break;
                case ComparisonType::NEQ: DST(bool2float(vmvnq_u32(vceqq_f32(SRC1, SRC2)))); break;
                case ComparisonType::NLT: DST(bool2float(vcgeq_f32(SRC1, SRC2))); break;
                case ComparisonType::NLE: DST(bool2float(vcgtq_f32(SRC1, SRC2))); break;
                }
                break;
            case ExprOpType::TERNARY: DST(vbslq_f32(float2bool(SRC1), SRC2, SRC3)); break;
            case ExprOpType::AND: DST(bool2float(vandq_u32(float2bool(SRC1), float2bool(SRC2)))); break;
            case ExprOpType::OR:  DST(bool2float(vorrq_u32(float2bool(SRC1), float2bool(SRC2)))); break;
            case ExprOpType::XOR: DST(bool2float(veorq_u32(float2bool(SRC1), float2bool(SRC2)))); break;
            case ExprOpType::NOT: DST(bool2float(vmvnq_u32(float2bool(SRC1)))); break;
            case ExprOpType::MEM_STORE_U8: {
                uint16x4_t narrowed = vmovn_u32(vreinterpretq_u32_s32(clamp_int(SRC1, 0.0f, 255.0f)));
                uint8x8_t bytes = vmovn_u16(vcombine_u16(narrowed, narrowed));
                uint32_t packed = vget_lane_u32(vreinterpret_u32_u8(bytes), 0);
                memcpy(reinterpret_cast<uint8_t *>(dstp) + x, &packed, sizeof(packed));
                goto next;
            }
            case ExprOpType::MEM_STORE_U16: {
                float maxval = static_cast<float>((1U << insn.op.imm.u) - 1);
                uint16x4_t narrowed = vmovn_u32(vreinterpretq_u32_s32(clamp_int(SRC1, 0.0f, maxval)));
                vst1_u16(reinterpret_cast<uint16_t *>(dstp) + x, narrowed);
                goto next;
            }
            case ExprOpType::MEM_STORE_F16:
                vst1_u16(reinterpret_cast<uint16_t *>(dstp) + x, vreinterpret_u16_f16(vcvt_f16_f32(SRC1)));
                goto next;
            case ExprOpType::MEM_STORE_F32:
                vst1q_f32(reinterpret_cast<float *>(dstp) + x, SRC1);
                goto next;
            default: fprintf(stderr, "%s", "illegal opcode\n"); std::terminate();
            }
#undef DST
#undef SRC3
#undef SRC2
#undef SRC1
        }
next:;
    }
    return x;
}

} // namespace expr
