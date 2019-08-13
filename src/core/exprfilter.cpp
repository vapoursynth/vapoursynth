/*
* Copyright (c) 2012-2019 Fredrik Mellbin
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

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "VapourSynth.h"
#include "VSHelper.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "kernel/cpulevel.h"

#ifdef VS_TARGET_CPU_X86
#include <immintrin.h>
#define NOMINMAX
#include "jitasm.h"
#ifndef VS_TARGET_OS_WINDOWS
#include <sys/mman.h>
#endif
#endif

namespace {

#define MAX_EXPR_INPUTS 26

enum class ExprOpType {
    // Terminals.
    MEM_LOAD_U8, MEM_LOAD_U16, MEM_LOAD_F16, MEM_LOAD_F32, CONSTANT,
    MEM_STORE_U8, MEM_STORE_U16, MEM_STORE_F16, MEM_STORE_F32,

    // Arithmetic primitives.
    ADD, SUB, MUL, DIV, SQRT, ABS, MAX, MIN, CMP,

    // Logical operators.
    AND, OR, XOR, NOT,

    // Transcendental functions.
    EXP, LOG, POW,

    // Ternary operator
    TERNARY,

    // Meta-node holding true/false branches of ternary.
    MUX,

    // Stack helpers.
    DUP, SWAP,
};

enum class ComparisonType {
    EQ = 0,
    LT = 1,
    LE = 2,
    NEQ = 4,
    NLT = 5,
    NLE = 6,
};

#ifdef VS_TARGET_CPU_X86
static_assert(static_cast<int>(ComparisonType::EQ) == _CMP_EQ_OQ, "");
static_assert(static_cast<int>(ComparisonType::LT) == _CMP_LT_OS, "");
static_assert(static_cast<int>(ComparisonType::LE) == _CMP_LE_OS, "");
static_assert(static_cast<int>(ComparisonType::NEQ) == _CMP_NEQ_UQ, "");
static_assert(static_cast<int>(ComparisonType::NLT) == _CMP_NLT_US, "");
static_assert(static_cast<int>(ComparisonType::NLE) == _CMP_NLE_US, "");
#endif

union ExprUnion {
    int32_t i;
    uint32_t u;
    float f;

    constexpr ExprUnion() : u{} {}

    constexpr ExprUnion(int32_t i) : i(i) {}
    constexpr ExprUnion(uint32_t u) : u(u) {}
    constexpr ExprUnion(float f) : f(f) {}
};

struct ExprOp {
    ExprOpType type;
    ExprUnion imm;

    ExprOp(ExprOpType type, ExprUnion param = {}) : type(type), imm(param) {}
};

enum PlaneOp {
    poProcess, poCopy, poUndefined
};

struct ExprData {
    VSNodeRef *node[MAX_EXPR_INPUTS];
    VSVideoInfo vi;
    std::vector<ExprOp> ops[3];
    int plane[3];
    size_t maxStackSize;
    int numInputs;
    typedef void (*ProcessLineProc)(void *rwptrs, intptr_t ptroff[MAX_EXPR_INPUTS + 1], intptr_t niter);
    ProcessLineProc proc[3];

    ExprData() : node(), vi(), plane(), maxStackSize(), numInputs(), proc() {}

    ~ExprData() {
#ifdef VS_TARGET_CPU_X86
        for (int i = 0; i < 3; i++) {
            if (proc[i]) {
#ifdef VS_TARGET_OS_WINDOWS
                VirtualFree((LPVOID)proc[i], 0, MEM_RELEASE);
#else
                munmap((void *)proc[i], 0);
#endif
            }
        }
#endif
    }
};

#ifdef VS_TARGET_CPU_X86
class ExprCompiler : private jitasm::function<void, ExprCompiler, uint8_t *, const intptr_t *, intptr_t> {
    typedef jitasm::function<void, ExprCompiler, uint8_t *, const intptr_t *, intptr_t> jit;
    friend struct jit;
    friend struct jitasm::function_cdecl<void, ExprCompiler, uint8_t *, const intptr_t *, intptr_t>;

#define SPLAT(x) { (x), (x), (x), (x) }
    static constexpr ExprUnion constData[31][4] alignas(16) = {
        SPLAT(0x7FFFFFFF), // absmask
        SPLAT(0x7F), // x7F
        SPLAT(0x00800000), // min_norm_pos
        SPLAT(~0x7F800000), // inv_mant_mask
        SPLAT(1.0f), // float_one
        SPLAT(0.5f), // float_half
        SPLAT(255.0f), // float_255
        SPLAT(65535.0f), // float_65535
        SPLAT(static_cast<int32_t>(0x80008000)), // i16min_epi16
        SPLAT(static_cast<int32_t>(0xFFFF8000)), // i16min_epi32
        SPLAT(88.3762626647949f), // exp_hi
        SPLAT(-88.3762626647949f), // exp_lo
        SPLAT(1.44269504088896341f), // log2e
        SPLAT(0.693359375f), // exp_c1
        SPLAT(-2.12194440e-4f), // exp_c2
        SPLAT(1.9875691500E-4f), // exp_p0
        SPLAT(1.3981999507E-3f), // exp_p1
        SPLAT(8.3334519073E-3f), // exp_p2
        SPLAT(4.1665795894E-2f), // exp_p3
        SPLAT(1.6666665459E-1f), // exp_p4
        SPLAT(5.0000001201E-1f), // exp_p5
        SPLAT(0.707106781186547524f), // sqrt_1_2
        SPLAT(7.0376836292E-2f), // log_p0
        SPLAT(-1.1514610310E-1f), // log_p1
        SPLAT(1.1676998740E-1f), // log_p2
        SPLAT(-1.2420140846E-1f), // log_p3
        SPLAT(+1.4249322787E-1f), // log_p4
        SPLAT(-1.6668057665E-1f), // log_p5
        SPLAT(+2.0000714765E-1f), // log_p6
        SPLAT(-2.4999993993E-1f), // log_p7
        SPLAT(+3.3333331174E-1f) // log_p8
    };

    struct ConstantIndex {
        static constexpr int absmask = 0;
        static constexpr int x7F = 1;
        static constexpr int min_norm_pos = 2;
        static constexpr int inv_mant_mask = 3;
        static constexpr int float_one = 4;
        static constexpr int float_half = 5;
        static constexpr int float_255 = 6;
        static constexpr int float_65535 = 7;
        static constexpr int i16min_epi16 = 8;
        static constexpr int i16min_epi32 = 9;
        static constexpr int exp_hi = 10;
        static constexpr int exp_lo = 11;
        static constexpr int log2e = 12;
        static constexpr int exp_c1 = 13;
        static constexpr int exp_c2 = 14;
        static constexpr int exp_p0 = 15;
        static constexpr int exp_p1 = 16;
        static constexpr int exp_p2 = 17;
        static constexpr int exp_p3 = 18;
        static constexpr int exp_p4 = 19;
        static constexpr int exp_p5 = 20;
        static constexpr int sqrt_1_2 = 21;
        static constexpr int log_p0 = 22;
        static constexpr int log_p1 = 23;
        static constexpr int log_p2 = 24;
        static constexpr int log_p3 = 25;
        static constexpr int log_p4 = 26;
        static constexpr int log_p5 = 27;
        static constexpr int log_p6 = 28;
        static constexpr int log_p7 = 29;
        static constexpr int log_p8 = 30;
        static constexpr int log_q1 = exp_c2;
        static constexpr int log_q2 = exp_c1;
    };
#undef SPLAT

    // JitASM compiles everything from main(), so record the operations for later.
    std::vector<std::function<void(Reg, XmmReg, Reg, std::vector<std::pair<XmmReg, XmmReg>> &)>> deferred;

    CPUFeatures cpuFeatures;
    int numInputs;
    int curLabel;

#define EMIT() [this, arg](Reg regptrs, XmmReg zero, Reg constants, std::vector<std::pair<XmmReg, XmmReg>> &stack)
#define VEX1(op, arg1, arg2) \
do { \
  if (cpuFeatures.avx) \
    v##op(arg1, arg2); \
  else \
    op(arg1, arg2); \
} while (0)
#define VEX1IMM(op, arg1, arg2, imm) \
do { \
  if (cpuFeatures.avx) { \
    v##op(arg1, arg2, imm); \
  } else if (arg1 == arg2) { \
    op(arg2, imm); \
  } else { \
    movdqa(arg1, arg2); \
    op(arg1, imm); \
  } \
} while (0)
#define VEX2(op, arg1, arg2, arg3) \
do { \
  if (cpuFeatures.avx) { \
    v##op(arg1, arg2, arg3); \
  } else if (arg1 == arg2) { \
    op(arg2, arg3); \
  } else if (arg1 != arg3) { \
    movdqa(arg1, arg2); \
    op(arg1, arg3); \
  } else { \
    XmmReg tmp; \
    movdqa(tmp, arg2); \
    op(tmp, arg3); \
    movdqa(arg1, tmp); \
  } \
} while (0)
#define VEX2IMM(op, arg1, arg2, arg3, imm) \
do { \
  if (cpuFeatures.avx) { \
    v##op(arg1, arg2, arg3, imm); \
  } else if (arg1 == arg2) { \
    op(arg2, arg3, imm); \
  } else if (arg1 != arg3) { \
    movdqa(arg1, arg2); \
    op(arg1, arg3, imm); \
  } else { \
    XmmReg tmp; \
    movdqa(tmp, arg2); \
    op(tmp, arg3, imm); \
    movdqa(arg1, tmp); \
  } \
} while (0)

    void load8(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            XmmReg r1, r2;
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (arg.i + 1)]);
            VEX1(movq, r1, mmword_ptr[a]);
            VEX2(punpcklbw, r1, r1, zero);
            VEX2(punpckhwd, r2, r1, zero);
            VEX2(punpcklwd, r1, r1, zero);
            VEX1(cvtdq2ps, r1, r1);
            VEX1(cvtdq2ps, r2, r2);
            stack.emplace_back(r1, r2);
        });
    }

    void load16(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            XmmReg r1, r2;
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (arg.i + 1)]);
            VEX1(movdqa, r1, xmmword_ptr[a]);
            VEX2(punpckhwd, r2, r1, zero);
            VEX2(punpcklwd, r1, r1, zero);
            VEX1(cvtdq2ps, r1, r1);
            VEX1(cvtdq2ps, r2, r2);
            stack.emplace_back(r1, r2);
        });
    }

    void loadF16(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            XmmReg r1, r2;
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (arg.i + 1)]);
            vcvtph2ps(r1, qword_ptr[a]);
            vcvtph2ps(r2, qword_ptr[a + 8]);
            stack.emplace_back(r1, r2);
        });
    }

    void loadF32(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            XmmReg r1, r2;
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (arg.i + 1)]);
            VEX1(movdqa, r1, xmmword_ptr[a]);
            VEX1(movdqa, r2, xmmword_ptr[a + 16]);
            stack.emplace_back(r1, r2);
        });
    }

    void loadConst(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            XmmReg r1, r2;
            Reg32 a;
            mov(a, arg.i);
            VEX1(movd, r1, a);
            VEX2IMM(shufps, r1, r1, r1, 0);
            VEX1(movaps, r2, r1);
            stack.emplace_back(r1, r2);
        });
    }

    void store8(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();

            XmmReg limit;
            Reg a;
            VEX1(movaps, limit, xmmword_ptr[constants + ConstantIndex::float_255 * 16]);
            VEX2(minps, t1.first, t1.first, limit);
            VEX2(minps, t1.second, t1.second, limit);
            VEX1(cvtps2dq, t1.first, t1.first);
            VEX1(cvtps2dq, t1.second, t1.second);
            VEX2(packssdw, t1.first, t1.first, t1.second);
            VEX2(packuswb, t1.first, t1.first, zero);
            mov(a, ptr[regptrs]);
            VEX1(movq, mmword_ptr[a], t1.first);
        });
    }

    void store16(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();

            XmmReg limit;
            Reg a;
            VEX1(movaps, limit, xmmword_ptr[constants + ConstantIndex::float_65535 * 16]);
            VEX2IMM(shufps, limit, limit, limit, 0);
            VEX2(minps, t1.first, t1.first, limit);
            VEX2(minps, t1.second, t1.second, limit);
            VEX1(cvtps2dq, t1.first, t1.first);
            VEX1(cvtps2dq, t1.second, t1.second);

            if (cpuFeatures.sse4_1) {
                VEX2(packusdw, t1.first, t1.first, t1.second);
            } else {
                VEX1(movaps, limit, xmmword_ptr[constants + ConstantIndex::i16min_epi32 * 16]);
                VEX2(paddd, t1.first, t1.first, limit);
                VEX2(paddd, t1.second, t1.second, limit);
                VEX2(packssdw, t1.first, t1.first, t1.second);
                VEX2(psubw, t1.first, t1.first, xmmword_ptr[constants + ConstantIndex::i16min_epi16 * 16]);
            }
        });
    }

    void storeF16(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();

            Reg a;
            mov(a, ptr[regptrs]);
            vcvtps2ph(qword_ptr[a], t1.first, 0);
            vcvtps2ph(qword_ptr[a + 8], t1.second, 0);
        });
    }

    void storeF32(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();

            Reg a;
            mov(a, ptr[regptrs]);
            VEX1(movaps, xmmword_ptr[a], t1.first);
            VEX1(movaps, xmmword_ptr[a + 16], t1.second);
        });
    }

    void dup(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto p = stack.at(stack.size() - arg.i);
            XmmReg r1, r2;
            VEX1(movaps, r1, p.first);
            VEX1(movaps, r2, p.second);
            stack.emplace_back(r1, r2);
        });
    }

#define BINARYOP(op) \
do { \
  auto t1 = stack.back(); \
  stack.pop_back(); \
  auto t2 = stack.back(); \
  VEX2(op, t2.first, t2.first, t1.first); \
  VEX2(op, t2.second, t2.second, t1.second); \
} while (0)
    void add(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(addps);
        });
    }

    void sub(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(subps);
        });
    }

    void mul(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(mulps);
        });
    }

    void div(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(divps);
        });
    }

    void max(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(maxps);
        });
    }

    void min(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(minps);
        });
    }
#undef BINARYOP

    void sqrt(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            VEX2(maxps, t1.first, t1.first, zero);
            VEX2(maxps, t1.second, t1.second, zero);
            VEX1(sqrtps, t1.first, t1.first);
            VEX1(sqrtps, t1.second, t1.second);
        });
    }

    void abs(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::absmask * 16]);
            VEX2(andps, t1.first, t1.first, r1);
            VEX2(andps, t1.second, t1.second, r1);
        });
    }

    void not_(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            VEX2IMM(cmpps, t1.first, t1.first, zero, _CMP_LT_OS);
            VEX2IMM(cmpps, t1.second, t1.second, zero, _CMP_LT_OS);
            VEX2(andps, t1.first, t1.first, r1);
            VEX2(andps, t1.second, t1.second, r1);
        });
    }

#define LOGICOP(op) \
do { \
  auto t1 = stack.back(); \
  stack.pop_back(); \
  auto t2 = stack.back(); \
  stack.pop_back(); \
  \
  XmmReg r1; \
  VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]); \
  VEX2IMM(cmpps, t1.first, t1.first, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, t1.second, t1.second, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, t2.first, t2.first, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, t2.second, t2.second, zero, _CMP_NLE_US); \
  VEX2(op, t1.first, t1.first, t2.first); \
  VEX2(op, t1.second, t1.second, t2.second); \
  VEX2(andps, t1.first, t1.first, r1); \
  VEX2(andps, t1.second, t1.second, r1); \
  stack.push_back(t1); \
} while (0)

    void and_(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(andps);
        });
    }

    void or_(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(orps);
        });
    }

    void xor_(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(xorps);
        });
    }
#undef LOGICOP

    void cmp(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();
            auto t2 = stack.back();
            stack.pop_back();

            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            VEX2IMM(cmpps, t2.first, t2.first, t1.first, arg.i);
            VEX2IMM(cmpps, t2.second, t2.second, t1.second, arg.i);
            VEX2(andps, t2.first, t2.first, r1);
            VEX2(andps, t2.second, t2.second, r1);
            stack.push_back(t2);
        });
    }

    void ternary(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            auto t1 = stack.back();
            stack.pop_back();
            auto t2 = stack.back();
            stack.pop_back();
            auto t3 = stack.back();
            stack.pop_back();

            VEX2IMM(cmpps, t3.first, t3.first, zero, _CMP_NLE_US);
            VEX2IMM(cmpps, t3.second, t3.second, zero, _CMP_NLE_US);

            if (cpuFeatures.sse4_1) {
                VEX2IMM(blendvps, t1.first, t1.first, t2.first, t3.first);
                VEX2IMM(blendvps, t1.second, t1.second, t2.second, t3.second);
                stack.push_back(t1);
            } else {
                VEX2(andps, t2.first, t2.first, t3.first);
                VEX2(andps, t2.second, t2.second, t3.second);
                VEX2(andnps, t3.first, t3.first, t1.first);
                VEX2(andnps, t3.second, t3.second, t1.second);
                VEX2(orps, t2.first, t2.first, t3.first);
                VEX2(orps, t2.second, t2.second, t3.second);
                stack.push_back(t2);
            }
        });
    }

    void exp_(XmmReg x, XmmReg one, Reg constants)
    {
        XmmReg fx, emm0, etmp, y, mask, z;
        VEX2(minps, x, x, xmmword_ptr[constants + ConstantIndex::exp_hi * 16]);
        VEX2(maxps, x, x, xmmword_ptr[constants + ConstantIndex::exp_lo * 16]);
        VEX2(mulps, fx, x, xmmword_ptr[constants + ConstantIndex::log2e * 16]);
        VEX2(addps, fx, fx, xmmword_ptr[constants + ConstantIndex::float_half * 16]);
        VEX1(cvttps2dq, emm0, fx);
        VEX1(cvtdq2ps, etmp, emm0);
        VEX2IMM(cmpps, mask, etmp, fx, _CMP_NLE_US);
        VEX2(andps, mask, mask, one);
        VEX2(subps, fx, etmp, mask);
        VEX2(mulps, etmp, fx, xmmword_ptr[constants + ConstantIndex::exp_c1 * 16]);
        VEX2(mulps, z, fx, xmmword_ptr[constants + ConstantIndex::exp_c2 * 16]);
        VEX2(subps, x, x, etmp);
        VEX2(subps, x, x, z);
        VEX2(mulps, z, x, x);
        VEX2(mulps, y, x, xmmword_ptr[constants + ConstantIndex::exp_p0 * 16]);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::exp_p1 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::exp_p2 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::exp_p3 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::exp_p4 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::exp_p5 * 16]);
        VEX2(mulps, y, y, z);
        VEX2(addps, y, y, x);
        VEX2(addps, y, y, one);
        VEX1(cvttps2dq, emm0, fx);
        VEX2(paddd, emm0, emm0, xmmword_ptr[constants + ConstantIndex::x7F * 16]);
        VEX1IMM(pslld, emm0, emm0, 23);
        VEX2(mulps, x, y, emm0);
    }

    void log_(XmmReg x, XmmReg zero, XmmReg one, Reg constants)
    {
        XmmReg emm0, invalid_mask, mask, y, etmp, z;
        VEX2IMM(cmpps, invalid_mask, zero, x, _CMP_NLE_US);
        VEX2(maxps, x, x, xmmword_ptr[constants + ConstantIndex::min_norm_pos * 16]);
        VEX1IMM(psrld, emm0, x, 23);
        VEX2(andps, x, x, xmmword_ptr[constants + ConstantIndex::inv_mant_mask * 16]);
        VEX2(orps, x, x, xmmword_ptr[constants + ConstantIndex::float_half * 16]);
        VEX2(psubd, emm0, emm0, xmmword_ptr[constants + ConstantIndex::x7F * 16]);
        VEX1(cvtdq2ps, emm0, emm0);
        VEX2(addps, emm0, emm0, one);
        VEX2IMM(cmpps, mask, x, xmmword_ptr[constants + ConstantIndex::sqrt_1_2 * 16], _CMP_LT_OS);
        VEX2(andps, etmp, x, mask);
        VEX2(subps, x, x, one);
        VEX2(andps, mask, mask, one);
        VEX2(subps, emm0, emm0, mask);
        VEX2(addps, x, x, etmp);
        VEX2(mulps, z, x, x);
        VEX2(mulps, y, x, xmmword_ptr[constants + ConstantIndex::log_p0 * 16]);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p1 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p2 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p3 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p4 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p5 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p6 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p7 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(addps, y, y, xmmword_ptr[constants + ConstantIndex::log_p8 * 16]);
        VEX2(mulps, y, y, x);
        VEX2(mulps, y, y, z);
        VEX2(mulps, etmp, emm0, xmmword_ptr[constants + ConstantIndex::log_q1 * 16]);
        VEX2(addps, y, y, etmp);
        VEX2(mulps, z, z, xmmword_ptr[constants + ConstantIndex::float_half * 16]);
        VEX2(subps, y, y, z);
        VEX2(mulps, emm0, emm0, xmmword_ptr[constants + ConstantIndex::log_q2 * 16]);
        VEX2(addps, x, x, y);
        VEX2(addps, x, x, emm0);
        VEX2(orps, x, x, invalid_mask);
    }

    void exp(ExprUnion arg)
    {
        int l = curLabel++;

        deferred.push_back([this, arg, l](Reg regptrs, XmmReg zero, Reg constants, std::vector<std::pair<XmmReg, XmmReg>> &stack)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = stack.back();
            XmmReg r1, r2, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            exp_(r1, one, constants);
            VEX1(movaps, t1.first, t1.second);
            VEX1(movaps, t1.second, r1);
            VEX1(movaps, r1, r2);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void log(ExprUnion arg)
    {
        int l = curLabel++;

        deferred.push_back([this, arg, l](Reg regptrs, XmmReg zero, Reg constants, std::vector<std::pair<XmmReg, XmmReg>> &stack)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = stack.back();
            XmmReg r1, r2, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            log_(r1, zero, one, constants);
            VEX1(movaps, t1.first, t1.second);
            VEX1(movaps, t1.second, r1);
            VEX1(movaps, r1, r2);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void pow(ExprUnion arg)
    {
        int l = curLabel++;

        deferred.push_back([this, arg, l](Reg regptrs, XmmReg zero, Reg constants, std::vector<std::pair<XmmReg, XmmReg>> &stack)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = stack.back();
            stack.pop_back();
            auto t2 = stack.back();

            XmmReg r1, r2, r3, r4, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, r3, t2.first);
            VEX1(movaps, r4, t2.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            log_(r3, zero, one, constants);
            VEX2(mulps, r3, r3, r1);
            exp_(r3, one, constants);

            VEX1(movaps, t2.first, t2.second);
            VEX1(movaps, t2.second, r3);
            VEX1(movaps, r1, r2);
            VEX1(movaps, r3, r4);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void main(Reg regptrs, Reg regoffs, Reg niter)
    {
        XmmReg zero;
        VEX2(pxor, zero, zero, zero);
        Reg constants;
        mov(constants, (uintptr_t)constData);

        L("wloop");

        std::vector<std::pair<XmmReg, XmmReg>> stack;

        for (const auto &f : deferred) {
            f(regptrs, zero, constants, stack);
        }

#if UINTPTR_MAX > UINT32_MAX
        for (int i = 0; i < numInputs / 2 + 1; i++) {
            XmmReg r1, r2;
            VEX1(movdqu, r1, xmmword_ptr[regptrs + 16 * i]);
            VEX1(movdqu, r2, xmmword_ptr[regoffs + 16 * i]);
            VEX2(paddq, r1, r1, r2);
            VEX1(movdqu, xmmword_ptr[regptrs + 16 * i], r1);
        }
#else
        for (int i = 0; i < numInputs / 4 + 1; i++) {
            XmmReg r1, r2;
            VEX1(movdqu, r1, xmmword_ptr[regptrs + 16 * i]);
            VEX1(movdqu, r2, xmmword_ptr[regoffs + 16 * i]);
            VEX2(paddd, r1, r1, r2);
            VEX1(movdqu, xmmword_ptr[regptrs + 16 * i], r1);
        }
#endif

        jit::sub(niter, 1);
        jnz("wloop");
    }

public:
    explicit ExprCompiler(int numInputs) : numInputs(numInputs), curLabel()
    {
        getCPUFeatures(&cpuFeatures);
    }

    void add_op(ExprOp op)
    {
        switch (op.type) {
        case ExprOpType::MEM_LOAD_U8: load8(op.imm); break;
        case ExprOpType::MEM_LOAD_U16: load16(op.imm); break;
        case ExprOpType::MEM_LOAD_F16: loadF16(op.imm); break;
        case ExprOpType::MEM_LOAD_F32: loadF32(op.imm); break;
        case ExprOpType::CONSTANT: loadConst(op.imm); break;
        case ExprOpType::MEM_STORE_U8: store8(op.imm); break;
        case ExprOpType::MEM_STORE_U16: store16(op.imm); break;
        case ExprOpType::MEM_STORE_F16: storeF16(op.imm); break;
        case ExprOpType::MEM_STORE_F32: storeF32(op.imm); break;
        case ExprOpType::DUP: dup(op.imm); break;
        case ExprOpType::ADD: add(op.imm); break;
        case ExprOpType::SUB: sub(op.imm); break;
        case ExprOpType::MUL: mul(op.imm); break;
        case ExprOpType::DIV: div(op.imm); break;
        case ExprOpType::MAX: max(op.imm); break;
        case ExprOpType::MIN: min(op.imm); break;
        case ExprOpType::SQRT: sqrt(op.imm); break;
        case ExprOpType::ABS: abs(op.imm); break;
        case ExprOpType::NOT: not_(op.imm); break;
        case ExprOpType::AND: and_(op.imm); break;
        case ExprOpType::OR: or_(op.imm); break;
        case ExprOpType::XOR: xor_(op.imm); break;
        case ExprOpType::CMP: cmp(op.imm); break;
        case ExprOpType::TERNARY: ternary(op.imm); break;
        case ExprOpType::EXP: exp(op.imm); break;
        case ExprOpType::LOG: log(op.imm); break;
        case ExprOpType::POW: pow(op.imm); break;
        }
    }

    ExprData::ProcessLineProc getCode() {
        if (jit::GetCode() && GetCodeSize()) {
#ifdef VS_TARGET_OS_WINDOWS
            void *ptr = VirtualAlloc(nullptr, GetCodeSize(), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
            void *ptr = mmap(nullptr, ExprObj.GetCodeSize(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#endif
            memcpy(ptr, jit::GetCode(), GetCodeSize());
            return reinterpret_cast<ExprData::ProcessLineProc>(ptr);
        }
        return nullptr;
    }
#undef VEX2IMM
#undef VEX2
#undef VEX1IMM
#undef VEX1
#undef EMIT
};
#endif

class ExprInterpreter {
    const ExprOp *vops;
    size_t numOps;
    std::vector<float> stack;
public:
    ExprInterpreter(const ExprOp *ops, size_t numOps, size_t stackSize) : vops(ops), numOps(numOps), stack(stackSize)
    {}

    void eval(const uint8_t * const *srcp, uint8_t *dstp, int x)
    {
        float stacktop;
        int si = 0;
        int i = -1;

        while (true) {
            i++;
            switch (vops[i].type) {
            case ExprOpType::MEM_LOAD_U8:
                stack[si] = stacktop;
                stacktop = srcp[vops[i].imm.i][x];
                ++si;
                break;
            case ExprOpType::MEM_LOAD_U16:
                stack[si] = stacktop;
                stacktop = reinterpret_cast<const uint16_t *>(srcp[vops[i].imm.i])[x];
                ++si;
                break;
            case ExprOpType::MEM_LOAD_F32:
                stack[si] = stacktop;
                stacktop = reinterpret_cast<const float *>(srcp[vops[i].imm.i])[x];
                ++si;
                break;
            case ExprOpType::CONSTANT:
                stack[si] = stacktop;
                stacktop = vops[i].imm.f;
                ++si;
                break;
            case ExprOpType::DUP:
                stack[si] = stacktop;
                stacktop = stack[si - vops[i].imm.i];
                ++si;
                break;
            case ExprOpType::SWAP:
                std::swap(stacktop, stack[si - vops[i].imm.i]);
                break;
            case ExprOpType::ADD:
                --si;
                stacktop += stack[si];
                break;
            case ExprOpType::SUB:
                --si;
                stacktop = stack[si] - stacktop;
                break;
            case ExprOpType::MUL:
                --si;
                stacktop *= stack[si];
                break;
            case ExprOpType::DIV:
                --si;
                stacktop = stack[si] / stacktop;
                break;
            case ExprOpType::MAX:
                --si;
                stacktop = std::max(stacktop, stack[si]);
                break;
            case ExprOpType::MIN:
                --si;
                stacktop = std::min(stacktop, stack[si]);
                break;
            case ExprOpType::EXP:
                stacktop = std::exp(stacktop);
                break;
            case ExprOpType::LOG:
                stacktop = std::log(stacktop);
                break;
            case ExprOpType::POW:
                --si;
                stacktop = std::pow(stack[si], stacktop);
                break;
            case ExprOpType::SQRT:
                stacktop = std::sqrt(stacktop);
                break;
            case ExprOpType::ABS:
                stacktop = std::fabs(stacktop);
                break;
            case ExprOpType::CMP:
                --si;
                switch (static_cast<ComparisonType>(vops[i].imm.i)) {
                case ComparisonType::EQ: stacktop = stack[si] == stacktop ? 1.0f : 0.0f; break;
                case ComparisonType::LT: stacktop = stack[si] < stacktop ? 1.0f : 0.0f; break;
                case ComparisonType::LE: stacktop = stack[si] <= stacktop ? 1.0f : 0.0f; break;
                case ComparisonType::NEQ: stacktop = stack[si] != stacktop ? 1.0f : 0.0f; break;
                case ComparisonType::NLT: stacktop = stack[si] >= stacktop ? 1.0f : 0.0f; break;
                case ComparisonType::NLE: stacktop = stack[si] > stacktop ? 1.0f : 0.0f; break;
                }
                break;
            case ExprOpType::TERNARY:
                si -= 2;
                stacktop = (stack[si] > 0) ? stack[si + 1] : stacktop;
                break;
            case ExprOpType::AND:
                --si;
                stacktop = (stacktop > 0 && stack[si] > 0) ? 1.0f : 0.0f;
                break;
            case ExprOpType::OR:
                --si;
                stacktop = (stacktop > 0 || stack[si] > 0) ? 1.0f : 0.0f;
                break;
            case ExprOpType::XOR:
                --si;
                stacktop = ((stacktop > 0) != (stack[si] > 0)) ? 1.0f : 0.0f;
                break;
            case ExprOpType::NOT:
                stacktop = (stacktop > 0) ? 0.0f : 1.0f;
                break;
            case ExprOpType::MEM_STORE_U8:
                dstp[x] = static_cast<uint8_t>(std::lrint(std::max(0.0f, std::min(stacktop, 255.0f))));
                return;
            case ExprOpType::MEM_STORE_U16:
                reinterpret_cast<uint16_t *>(dstp)[x] = static_cast<uint16_t>(std::lrint(std::max(0.0f, std::min(stacktop, 65535.0f))));
                return;
            case ExprOpType::MEM_STORE_F32:
                reinterpret_cast<float *>(dstp)[x] = stacktop;
                return;
            }
        }
    }
};

struct ExpressionTreeNode {
    ExpressionTreeNode *parent;
    ExpressionTreeNode *left;
    ExpressionTreeNode *right;
    ExprOp op;
    int valueNum;

    explicit ExpressionTreeNode(ExprOp op) : parent(), left(), right(), op(op), valueNum(-1) {}

    void setLeft(ExpressionTreeNode *node)
    {
        if (left)
            left->parent = nullptr;

        left = node;

        if (left)
            left->parent = this;
    }

    void setRight(ExpressionTreeNode *node)
    {
        if (right)
            right->parent = nullptr;

        right = node;

        if (right)
            right->parent = this;
    }

    template <class T>
    void postorder(T visitor)
    {
        if (left)
            left->postorder(visitor);
        if (right)
            right->postorder(visitor);
        visitor(*this);
    }
};

class ExpressionTree {
    std::vector<std::unique_ptr<ExpressionTreeNode>> nodes;
    ExpressionTreeNode *root;
public:
    ExpressionTree() : root() {}

    ExpressionTreeNode *getRoot() { return root; }
    const ExpressionTreeNode *getRoot() const { return root; }

    void setRoot(ExpressionTreeNode *node) { root = node; }

    ExpressionTreeNode *makeNode(ExprOp data)
    {
        nodes.push_back(std::unique_ptr<ExpressionTreeNode>(new ExpressionTreeNode(data)));
        return nodes.back().get();
    }

    ExpressionTreeNode *clone(const ExpressionTreeNode *node)
    {
        if (!node)
            return nullptr;

        ExpressionTreeNode *newnode = makeNode(node->op);
        newnode->setLeft(clone(node->left));
        newnode->setRight(clone(node->right));
        return newnode;
    }
};

std::unique_ptr<ExpressionTreeNode> makeTreeNode(ExprOp data)
{
    return std::unique_ptr<ExpressionTreeNode>(new ExpressionTreeNode(data));
}

std::vector<std::string> tokenize(const std::string &expr)
{
    std::vector<std::string> tokens;
    auto it = expr.begin();
    auto prev = expr.begin();

    while (it != expr.end()) {
        char c = *it;

        if (std::isspace(c)) {
            if (it != prev)
                tokens.push_back(expr.substr(prev - expr.begin(), it - prev));
            prev = it + 1;
        }
        ++it;
    }
    if (prev != expr.end())
        tokens.push_back(expr.substr(prev - expr.begin(), expr.end() - prev));

    return tokens;
}

ExprOp decodeToken(const std::string &token)
{
    static const std::unordered_map<std::string, ExprOp> simple{
        { "+",    { ExprOpType::ADD } },
        { "-",    { ExprOpType::SUB } },
        { "*",    { ExprOpType::MUL } },
        { "/",    { ExprOpType::DIV } } ,
        { "sqrt", { ExprOpType::SQRT } },
        { "abs",  { ExprOpType::ABS } },
        { "max",  { ExprOpType::MAX } },
        { "min",  { ExprOpType::MIN } },
        { "<",    { ExprOpType::CMP, static_cast<int>(ComparisonType::LT) } },
        { ">",    { ExprOpType::CMP, static_cast<int>(ComparisonType::NLE) } },
        { "=",    { ExprOpType::CMP, static_cast<int>(ComparisonType::EQ) } },
        { ">=",   { ExprOpType::CMP, static_cast<int>(ComparisonType::NLT) } },
        { "<=",   { ExprOpType::CMP, static_cast<int>(ComparisonType::LE) } },
        { "and",  { ExprOpType::AND } },
        { "or",   { ExprOpType::OR } },
        { "xor",  { ExprOpType::XOR } },
        { "not",  { ExprOpType::NOT } },
        { "?",    { ExprOpType::TERNARY } },
        { "exp",  { ExprOpType::EXP } },
        { "log",  { ExprOpType::LOG } },
        { "pow",  { ExprOpType::POW } },
        { "dup",  { ExprOpType::DUP, 0 } },
        { "swap", { ExprOpType::SWAP, 1 } },
    };

    auto it = simple.find(token);
    if (it != simple.end()) {
        return it->second;
    } else if (token.size() == 1 && token[0] >= 'a' && token[0] <= 'z') {
        return{ ExprOpType::MEM_LOAD_U8, token[0] >= 'x' ? token[0] - 'x' : token[0] - 'a' + 3 };
    } else if (token.substr(0, 3) == "dup" || token.substr(0, 4) == "swap") {
        size_t count;
        int idx = -1;

        try {
            idx = std::stoi(token.substr(token[0] == 'd' ? 3 : 4), &count);
        } catch (...) {
            // ...
        }

        if (idx < 0)
            throw std::runtime_error("illegal token: " + token);
        return{ token[0] == 'd' ? ExprOpType::DUP : ExprOpType::SWAP, idx };
    } else {
        float f;
        std::string s;
        std::istringstream numStream(token);
        numStream.imbue(std::locale::classic());
        if (!(numStream >> f))
            throw std::runtime_error("failed to convert '" + token + "' to float");
        if (numStream >> s)
            throw std::runtime_error("failed to convert '" + token + "' to float, not the whole token could be converted");
        return{ ExprOpType::CONSTANT, f };
    }
}

ExpressionTree parseExpr(const std::string &expr, const VSVideoInfo * const *vi, int numInputs)
{
    constexpr unsigned char numOperands[] = {
        0, // MEM_LOAD_U8
        0, // MEM_LOAD_U16
        0, // MEM_LOAD_F16
        0, // MEM_LOAD_F32
        0, // CONSTANT
        0, // MEM_STORE_U8
        0, // MEM_STORE_U16
        0, // MEM_STORE_F16
        0, // MEM_STORE_F32
        2, // ADD
        2, // SUB
        2, // MUL
        2, // DIV
        1, // SQRT
        1, // ABS
        2, // MAX
        2, // MIN
        2, // CMP
        2, // AND
        2, // OR
        2, // XOR
        2, // NOT
        1, // EXP
        1, // LOG
        2, // POW
        3, // TERNARY
        0, // MUX
        0, // DUP
        0, // SWAP
    };
    static_assert(sizeof(numOperands) == static_cast<unsigned>(ExprOpType::SWAP) + 1, "invalid table");

    auto tokens = tokenize(expr);

    ExpressionTree tree;
    std::vector<ExpressionTreeNode *> stack;

    for (const std::string &tok : tokens) {
        ExprOp op = decodeToken(tok);

        // Check validity.
        if (op.type == ExprOpType::MEM_LOAD_U8 && op.imm.i >= numInputs)
            throw std::runtime_error("reference to undefined clip: " + tok);
        if ((op.type == ExprOpType::DUP || op.type == ExprOpType::SWAP) && op.imm.u >= stack.size())
            throw std::runtime_error("insufficient values on stack: " + tok);
        if (stack.size() < numOperands[static_cast<size_t>(op.type)])
            throw std::runtime_error("insufficient values on stack: " + tok);

        // Rename load operations with the correct data type.
        if (op.type == ExprOpType::MEM_LOAD_U8) {
            const VSFormat *format = vi[op.imm.i]->format;

            if (format->sampleType == stInteger && format->bytesPerSample == 1)
                op.type = ExprOpType::MEM_LOAD_U8;
            else if (format->sampleType == stInteger && format->bytesPerSample == 2)
                op.type = ExprOpType::MEM_LOAD_U16;
            else if (format->sampleType == stFloat && format->bytesPerSample == 2)
                op.type = ExprOpType::MEM_LOAD_F16;
            else if (format->sampleType == stFloat && format->bytesPerSample == 4)
                op.type = ExprOpType::MEM_LOAD_F32;
        }

        // Apply DUP and SWAP in the frontend.
        if (op.type == ExprOpType::DUP) {
            stack.push_back(tree.clone(stack[stack.size() - 1 - op.imm.u]));
        } else if (op.type == ExprOpType::SWAP) {
            std::swap(stack.back(), stack[stack.size() - 1 - op.imm.u]);
        } else {
            size_t operands = numOperands[static_cast<size_t>(op.type)];

            if (operands == 0) {
                stack.push_back(tree.makeNode(op));
            } else if (operands == 1) {
                ExpressionTreeNode *child = stack.back();
                stack.pop_back();

                ExpressionTreeNode *node = tree.makeNode(op);
                node->setLeft(child);
                stack.push_back(node);
            } else if (operands == 2) {
                ExpressionTreeNode *left = stack[stack.size() - 2];
                ExpressionTreeNode *right = stack[stack.size() - 1];
                stack.resize(stack.size() - 2);

                ExpressionTreeNode *node = tree.makeNode(op);
                node->setLeft(left);
                node->setRight(right);
                stack.push_back(node);
            } else if (operands == 3) {
                ExpressionTreeNode *arg1 = stack[stack.size() - 3];
                ExpressionTreeNode *arg2 = stack[stack.size() - 2];
                ExpressionTreeNode *arg3 = stack[stack.size() - 1];
                stack.resize(stack.size() - 3);

                ExpressionTreeNode *mux = tree.makeNode(ExprOpType::MUX);
                mux->setLeft(arg2);
                mux->setRight(arg3);

                ExpressionTreeNode *node = tree.makeNode(op);
                node->setLeft(arg1);
                node->setRight(mux);
                stack.push_back(node);
            }
        }
    }

    if (stack.empty())
        throw std::runtime_error("empty expression: " + expr);
    if (stack.size() > 1)
        throw std::runtime_error("unconsumed values on stack: " + expr);

    tree.setRoot(stack.back());
    return tree;
}

std::vector<ExprOp> serializeTree(const ExpressionTreeNode *root, const VSFormat *format)
{
    std::vector<ExprOp> bytecode;

    const_cast<ExpressionTreeNode *>(root)->postorder([&](const ExpressionTreeNode &node)
    {
        if (node.op.type == ExprOpType::MUX)
            return;

        bytecode.push_back(node.op);
    });

    // Add final store.
    if (format->sampleType == stInteger && format->bytesPerSample == 1)
        bytecode.push_back(ExprOpType::MEM_STORE_U8);
    else if (format->sampleType == stInteger && format->bytesPerSample == 2)
        bytecode.push_back(ExprOpType::MEM_STORE_U16);
    else if (format->sampleType == stFloat && format->bytesPerSample == 2)
        bytecode.push_back(ExprOpType::MEM_STORE_F16);
    else if (format->sampleType == stFloat && format->bytesPerSample == 4)
        bytecode.push_back(ExprOpType::MEM_STORE_F32);

    return bytecode;
}

static void VS_CC exprInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ExprData *d = static_cast<ExprData *>(*instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC exprGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ExprData *d = static_cast<ExprData *>(*instanceData);
    int numInputs = d->numInputs;

    if (activationReason == arInitial) {
        for (int i = 0; i < numInputs; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < numInputs; i++)
            src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);

        const VSFormat *fi = d->vi.format;
        int height = vsapi->getFrameHeight(src[0], 0);
        int width = vsapi->getFrameWidth(src[0], 0);
        int planes[3] = { 0, 1, 2 };
        const VSFrameRef *srcf[3] = { d->plane[0] != poCopy ? nullptr : src[0], d->plane[1] != poCopy ? nullptr : src[0], d->plane[2] != poCopy ? nullptr : src[0] };
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, width, height, srcf, planes, src[0], core);

        const uint8_t *srcp[MAX_EXPR_INPUTS] = {};
        int src_stride[MAX_EXPR_INPUTS] = {};
        intptr_t ptroffsets[MAX_EXPR_INPUTS + 1] = { d->vi.format->bytesPerSample * 8 };

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->plane[plane] != poProcess)
                continue;

            for (int i = 0; i < numInputs; i++) {
                if (d->node[i]) {
                    srcp[i] = vsapi->getReadPtr(src[i], plane);
                    src_stride[i] = vsapi->getStride(src[i], plane);
                    ptroffsets[i + 1] = vsapi->getFrameFormat(src[i])->bytesPerSample * 8;
                }
            }

            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(dst, plane);
            int w = vsapi->getFrameWidth(dst, plane);

            if (d->proc[plane]) {
                ExprData::ProcessLineProc proc = d->proc[plane];
                int niterations = (w + 7) / 8;

                for (int i = 0; i < numInputs; i++) {
                    if (d->node[i])
                        ptroffsets[i + 1] = vsapi->getFrameFormat(src[i])->bytesPerSample * 8;
                }

                for (int y = 0; y < h; y++) {
                    uint8_t *rwptrs[MAX_EXPR_INPUTS + 1] = { dstp + dst_stride * y };
                    for (int i = 0; i < numInputs; i++) {
                        rwptrs[i + 1] = const_cast<uint8_t *>(srcp[i] + src_stride[i] * y);
                    }
                    proc(rwptrs, ptroffsets, niterations);
                }
            } else {
                ExprInterpreter interpreter(d->ops[plane].data(), d->ops[plane].size(), d->maxStackSize);

                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        interpreter.eval(srcp, dstp, x);
                    }

                    for (int i = 0; i < numInputs; i++) {
                        srcp[i] += src_stride[i];
                    }
                    dstp += dst_stride;
                }
            }
        }

        for (int i = 0; i < MAX_EXPR_INPUTS; i++) {
            vsapi->freeFrame(src[i]);
        }
        return dst;
    }

    return nullptr;
}

static void VS_CC exprFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ExprData *d = static_cast<ExprData *>(instanceData);
    for (int i = 0; i < MAX_EXPR_INPUTS; i++)
        vsapi->freeNode(d->node[i]);
    delete d;
}

static void VS_CC exprCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ExprData> d(new ExprData);
    int err;

#ifdef VS_TARGET_CPU_X86
    CPUFeatures f;
    getCPUFeatures(&f);
#   define EXPR_F16C_TEST (f.f16c)
#else
#   define EXPR_F16C_TEST (false)
#endif

    try {
        d->numInputs = vsapi->propNumElements(in, "clips");
        if (d->numInputs > 26)
            throw std::runtime_error("More than 26 input clips provided");

        for (int i = 0; i < d->numInputs; i++) {
            d->node[i] = vsapi->propGetNode(in, "clips", i, &err);
        }

        const VSVideoInfo *vi[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < d->numInputs; i++) {
            if (d->node[i])
                vi[i] = vsapi->getVideoInfo(d->node[i]);
        }

        for (int i = 0; i < d->numInputs; i++) {
            if (!isConstantFormat(vi[i]))
                throw std::runtime_error("Only clips with constant format and dimensions allowed");
            if (vi[0]->format->numPlanes != vi[i]->format->numPlanes
                || vi[0]->format->subSamplingW != vi[i]->format->subSamplingW
                || vi[0]->format->subSamplingH != vi[i]->format->subSamplingH
                || vi[0]->width != vi[i]->width
                || vi[0]->height != vi[i]->height)
            {
                throw std::runtime_error("All inputs must have the same number of planes and the same dimensions, subsampling included");
            }

            if (EXPR_F16C_TEST) {
                if ((vi[i]->format->bitsPerSample > 16 && vi[i]->format->sampleType == stInteger)
                    || (vi[i]->format->bitsPerSample != 16 && vi[i]->format->bitsPerSample != 32 && vi[i]->format->sampleType == stFloat))
                    throw std::runtime_error("Input clips must be 8-16 bit integer or 16/32 bit float format");
            } else {
                if ((vi[i]->format->bitsPerSample > 16 && vi[i]->format->sampleType == stInteger)
                    || (vi[i]->format->bitsPerSample != 32 && vi[i]->format->sampleType == stFloat))
                    throw std::runtime_error("Input clips must be 8-16 bit integer or 32 bit float format");
            }
        }

        d->vi = *vi[0];
        int format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));
        if (!err) {
            const VSFormat *f = vsapi->getFormatPreset(format, core);
            if (f) {
                if (d->vi.format->colorFamily == cmCompat)
                    throw std::runtime_error("No compat formats allowed");
                if (d->vi.format->numPlanes != f->numPlanes)
                    throw std::runtime_error("The number of planes in the inputs and output must match");
                d->vi.format = vsapi->registerFormat(d->vi.format->colorFamily, f->sampleType, f->bitsPerSample, d->vi.format->subSamplingW, d->vi.format->subSamplingH, core);
            }
        }

        int nexpr = vsapi->propNumElements(in, "expr");
        if (nexpr > d->vi.format->numPlanes)
            throw std::runtime_error("More expressions given than there are planes");

        std::string expr[3];
        for (int i = 0; i < nexpr; i++) {
            expr[i] = vsapi->propGetData(in, "expr", i, nullptr);
        }
        for (int i = nexpr; i < 3; ++i) {
            expr[i] = expr[nexpr - 1];
        }

        for (int i = 0; i < 3; i++) {
            if (!expr[i].empty()) {
                d->plane[i] = poProcess;
            } else {
                if (d->vi.format->bitsPerSample == vi[0]->format->bitsPerSample && d->vi.format->sampleType == vi[0]->format->sampleType)
                    d->plane[i] = poCopy;
                else
                    d->plane[i] = poUndefined;
            }

            if (d->plane[i] != poProcess)
                continue;

            auto tree = parseExpr(expr[i], vi, d->numInputs);
            d->ops[i] = serializeTree(tree.getRoot(), d->vi.format);

            if (vs_get_cpulevel(core) > VS_CPU_LEVEL_NONE) {
                for (int i = 0; i < d->vi.format->numPlanes; i++) {
                    if (d->plane[i] == poProcess) {
#ifdef VS_TARGET_CPU_X86
                        ExprCompiler compiler(d->numInputs);
                        for (auto op : d->ops[i]) {
                            compiler.add_op(op);
                        }

                        d->proc[i] = compiler.getCode();
#endif
                    }
                }
            }
        }
#ifdef VS_TARGET_OS_WINDOWS
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
#endif
    } catch (std::runtime_error &e) {
        for (int i = 0; i < MAX_EXPR_INPUTS; i++)
            vsapi->freeNode(d->node[i]);
        std::string s = "Expr: ";
        s += e.what();
        vsapi->setError(out, s.c_str());
        return;
    }

    vsapi->createFilter(in, out, "Expr", exprInit, exprGetFrame, exprFree, fmParallel, 0, d.release(), core);
}

} // namespace


//////////////////////////////////////////
// Init

void VS_CC exprInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.expr", "expr", "VapourSynth Expr Filter", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Expr", "clips:clip[];expr:data[];format:int:opt;", exprCreate, nullptr, plugin);
}
