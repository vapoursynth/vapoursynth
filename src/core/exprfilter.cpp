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

struct split1 {
    enum empties_t { empties_ok, no_empties };
};

template <typename Container>
Container& split(
    Container& result,
    const typename Container::value_type& s,
    const typename Container::value_type& delimiters,
    split1::empties_t empties = split1::empties_ok)
{
    result.clear();
    size_t current;
    size_t next = -1;
    do {
        if (empties == split1::no_empties) {
            next = s.find_first_not_of(delimiters, next + 1);
            if (next == Container::value_type::npos) break;
            next -= 1;
        }
        current = next + 1;
        next = s.find_first_of( delimiters, current );
        result.push_back(s.substr(current, next - current));
    } while (next != Container::value_type::npos);
    return result;
}

enum SOperation {
    opLoadSrc8, opLoadSrc16, opLoadSrcF32, opLoadSrcF16, opLoadConst,
    opStore8, opStore16, opStoreF32, opStoreF16,
    opDup, opSwap,
    opAdd, opSub, opMul, opDiv, opMax, opMin, opSqrt, opAbs,
    opGt, opLt, opEq, opLE, opGE, opTernary,
    opAnd, opOr, opXor, opNeg,
    opExp, opLog, opPow
};

union ExprUnion {
    float fval;
    int32_t ival;

    ExprUnion() = default;
    constexpr ExprUnion(int32_t i) : ival(i) {}
    constexpr ExprUnion(float f) : fval(f) {}
};

struct ExprOp {
    ExprUnion e;
    uint32_t op;
    ExprOp(SOperation op, float val) : op(op) {
        e.fval = val;
    }
    ExprOp(SOperation op, int32_t val = 0) : op(op) {
        e.ival = val;
    }
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
            mov(a, ptr[regptrs + sizeof(void *) * (arg.ival + 1)]);
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
            mov(a, ptr[regptrs + sizeof(void *) * (arg.ival + 1)]);
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
            mov(a, ptr[regptrs + sizeof(void *) * (arg.ival + 1)]);
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
            mov(a, ptr[regptrs + sizeof(void *) * (arg.ival + 1)]);
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
            mov(a, arg.ival);
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
            auto p = stack.at(stack.size() - arg.ival);
            XmmReg r1, r2;
            VEX1(movaps, r1, p.first);
            VEX1(movaps, r2, p.second);
            stack.emplace_back(r1, r2);
        });
    }

    void swap(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            std::swap(stack.back(), stack.at(stack.size() - arg.ival));
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

    void neg(ExprUnion arg)
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

#define COMPAREOP(imm) \
do { \
  auto t1 = stack.back(); \
  stack.pop_back(); \
  auto t2 = stack.back(); \
  stack.pop_back(); \
  \
  XmmReg r1; \
  VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]); \
  VEX2IMM(cmpps, t2.first, t2.first, t1.first, imm); \
  VEX2IMM(cmpps, t2.second, t2.second, t1.second, imm); \
  VEX2(andps, t2.first, t2.first, r1); \
  VEX2(andps, t2.second, t2.second, r1); \
  stack.push_back(t2); \
} while (0)

    void cmpgt(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            COMPAREOP(_CMP_NLE_US);
        });
    }

    void cmplt(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            COMPAREOP(_CMP_LT_OS);
        });
    }

    void cmpeq(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            COMPAREOP(_CMP_EQ_OQ);
        });
    }

    void cmple(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            COMPAREOP(_CMP_LE_OS);
        });
    }

    void cmpge(ExprUnion arg)
    {
        deferred.push_back(EMIT()
        {
            COMPAREOP(_CMP_NLT_US);
        });
    }
#undef COMPAREOP

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
        switch (op.op) {
        case opLoadSrc8: load8(op.e); break;
        case opLoadSrc16: load16(op.e); break;
        case opLoadSrcF16: loadF16(op.e); break;
        case opLoadSrcF32: loadF32(op.e); break;
        case opLoadConst: loadConst(op.e); break;
        case opStore8: store8(op.e); break;
        case opStore16: store16(op.e); break;
        case opStoreF16: storeF16(op.e); break;
        case opStoreF32: storeF32(op.e); break;
        case opDup: dup(op.e); break;
        case opSwap: swap(op.e); break;
        case opAdd: add(op.e); break;
        case opSub: sub(op.e); break;
        case opMul: mul(op.e); break;
        case opDiv: div(op.e); break;
        case opMax: max(op.e); break;
        case opMin: min(op.e); break;
        case opSqrt: sqrt(op.e); break;
        case opAbs: abs(op.e); break;
        case opNeg: neg(op.e); break;
        case opAnd: and_(op.e); break;
        case opOr: or_(op.e); break;
        case opXor: xor_(op.e); break;
        case opGt: cmpgt(op.e); break;
        case opLt: cmplt(op.e); break;
        case opEq: cmpeq(op.e); break;
        case opLE: cmple(op.e); break;
        case opGE: cmpge(op.e); break;
        case opTernary: ternary(op.e); break;
        case opExp: exp(op.e); break;
        case opLog: log(op.e); break;
        case opPow: pow(op.e); break;
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
            switch (vops[i].op) {
            case opLoadSrc8:
                stack[si] = stacktop;
                stacktop = srcp[vops[i].e.ival][x];
                ++si;
                break;
            case opLoadSrc16:
                stack[si] = stacktop;
                stacktop = reinterpret_cast<const uint16_t *>(srcp[vops[i].e.ival])[x];
                ++si;
                break;
            case opLoadSrcF32:
                stack[si] = stacktop;
                stacktop = reinterpret_cast<const float *>(srcp[vops[i].e.ival])[x];
                ++si;
                break;
            case opLoadConst:
                stack[si] = stacktop;
                stacktop = vops[i].e.fval;
                ++si;
                break;
            case opDup:
                stack[si] = stacktop;
                stacktop = stack[si - vops[i].e.ival];
                ++si;
                break;
            case opSwap:
                std::swap(stacktop, stack[si - vops[i].e.ival]);
                break;
            case opAdd:
                --si;
                stacktop += stack[si];
                break;
            case opSub:
                --si;
                stacktop = stack[si] - stacktop;
                break;
            case opMul:
                --si;
                stacktop *= stack[si];
                break;
            case opDiv:
                --si;
                stacktop = stack[si] / stacktop;
                break;
            case opMax:
                --si;
                stacktop = std::max(stacktop, stack[si]);
                break;
            case opMin:
                --si;
                stacktop = std::min(stacktop, stack[si]);
                break;
            case opExp:
                stacktop = std::exp(stacktop);
                break;
            case opLog:
                stacktop = std::log(stacktop);
                break;
            case opPow:
                --si;
                stacktop = std::pow(stack[si], stacktop);
                break;
            case opSqrt:
                stacktop = std::sqrt(stacktop);
                break;
            case opAbs:
                stacktop = std::abs(stacktop);
                break;
            case opGt:
                --si;
                stacktop = (stack[si] > stacktop) ? 1.0f : 0.0f;
                break;
            case opLt:
                --si;
                stacktop = (stack[si] < stacktop) ? 1.0f : 0.0f;
                break;
            case opEq:
                --si;
                stacktop = (stack[si] == stacktop) ? 1.0f : 0.0f;
                break;
            case opLE:
                --si;
                stacktop = (stack[si] <= stacktop) ? 1.0f : 0.0f;
                break;
            case opGE:
                --si;
                stacktop = (stack[si] >= stacktop) ? 1.0f : 0.0f;
                break;
            case opTernary:
                si -= 2;
                stacktop = (stack[si] > 0) ? stack[si + 1] : stacktop;
                break;
            case opAnd:
                --si;
                stacktop = (stacktop > 0 && stack[si] > 0) ? 1.0f : 0.0f;
                break;
            case opOr:
                --si;
                stacktop = (stacktop > 0 || stack[si] > 0) ? 1.0f : 0.0f;
                break;
            case opXor:
                --si;
                stacktop = ((stacktop > 0) != (stack[si] > 0)) ? 1.0f : 0.0f;
                break;
            case opNeg:
                stacktop = (stacktop > 0) ? 0.0f : 1.0f;
                break;
            case opStore8:
                dstp[x] = static_cast<uint8_t>(std::lrint(std::max(0.0f, std::min(stacktop, 255.0f))));
                return;
            case opStore16:
                reinterpret_cast<uint16_t *>(dstp)[x] = static_cast<uint16_t>(std::lrint(std::max(0.0f, std::min(stacktop, 65535.0f))));
                return;
            case opStoreF32:
                reinterpret_cast<float *>(dstp)[x] = stacktop;
                return;
            }
        }
    }
};

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

static SOperation getLoadOp(const VSVideoInfo *vi) {
    if (!vi)
        return opLoadSrcF32;
    if (vi->format->sampleType == stInteger) {
        if (vi->format->bitsPerSample == 8)
            return opLoadSrc8;
        else
            return opLoadSrc16;
    } else {
        if (vi->format->bitsPerSample == 16)
            return opLoadSrcF16;
        else
            return opLoadSrcF32;
    }
}

static SOperation getStoreOp(const VSVideoInfo *vi) {
    if (!vi)
        return opLoadSrcF32;
    if (vi->format->sampleType == stInteger) {
        if (vi->format->bitsPerSample == 8)
            return opStore8;
        return opStore16;
    } else {
        if (vi->format->bitsPerSample == 16)
            return opStoreF16;
        else
            return opStoreF32;
    }
}

#define LOAD_OP(op,v,req) do { if (stackSize < req) throw std::runtime_error("Not enough elements on stack to perform operation " + tokens[i]); ops.push_back(ExprOp(op, (v))); maxStackSize = std::max(++stackSize, maxStackSize); } while(0)
#define GENERAL_OP(op, v, req, dec) do { if (stackSize < req) throw std::runtime_error("Not enough elements on stack to perform operation " + tokens[i]); ops.push_back(ExprOp(op, (v))); stackSize-=(dec); } while(0)
#define ONE_ARG_OP(op) GENERAL_OP(op, 0, 1, 0)
#define TWO_ARG_OP(op) GENERAL_OP(op, 0, 2, 1)
#define THREE_ARG_OP(op) GENERAL_OP(op, 0, 3, 2)

static size_t parseExpression(const std::string &expr, std::vector<ExprOp> &ops, const VSVideoInfo **vi, const VSVideoInfo *outputVi, int numInputs) {
    std::vector<std::string> tokens;
    split(tokens, expr, " ", split1::no_empties);

    int maxStackSize = 0;
    int stackSize = 0;

    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "+")
            TWO_ARG_OP(opAdd);
        else if (tokens[i] == "-")
            TWO_ARG_OP(opSub);
        else if (tokens[i] == "*")
            TWO_ARG_OP(opMul);
        else if (tokens[i] == "/")
            TWO_ARG_OP(opDiv);
        else if (tokens[i] == "max")
            TWO_ARG_OP(opMax);
        else if (tokens[i] == "min")
            TWO_ARG_OP(opMin);
        else if (tokens[i] == "exp")
            ONE_ARG_OP(opExp);
        else if (tokens[i] == "log")
            ONE_ARG_OP(opLog);
        else if (tokens[i] == "pow")
            TWO_ARG_OP(opPow);
        else if (tokens[i] == "sqrt")
            ONE_ARG_OP(opSqrt);
        else if (tokens[i] == "abs")
            ONE_ARG_OP(opAbs);
        else if (tokens[i] == ">")
            TWO_ARG_OP(opGt);
        else if (tokens[i] == "<")
            TWO_ARG_OP(opLt);
        else if (tokens[i] == "=")
            TWO_ARG_OP(opEq);
        else if (tokens[i] == ">=")
            TWO_ARG_OP(opGE);
        else if (tokens[i] == "<=")
            TWO_ARG_OP(opLE);
        else if (tokens[i] == "?")
            THREE_ARG_OP(opTernary);
        else if (tokens[i] == "and")
            TWO_ARG_OP(opAnd);
        else if (tokens[i] == "or")
            TWO_ARG_OP(opOr);
        else if (tokens[i] == "xor")
            TWO_ARG_OP(opXor);
        else if (tokens[i] == "not")
            ONE_ARG_OP(opNeg);
        else if (tokens[i].substr(0, 3) == "dup")
            if (tokens[i].size() == 3) {
                LOAD_OP(opDup, 0, 1);
            } else {
                try {
                    int tmp = std::stoi(tokens[i].substr(3));
                    if (tmp < 0)
                        throw std::runtime_error("Dup suffix can't be less than 0 '" + tokens[i] + "'");
                    LOAD_OP(opDup, tmp, tmp + 1);
                } catch (std::logic_error &) {
                    throw std::runtime_error("Failed to convert dup suffix '" + tokens[i] + "' to valid index");
                }
            }
        else if (tokens[i].substr(0, 4) == "swap")
            if (tokens[i].size() == 4) {
                GENERAL_OP(opSwap, 1, 2, 0);
            } else {
                try {
                    int tmp = std::stoi(tokens[i].substr(4));
                    if (tmp < 1)
                        throw std::runtime_error("Swap suffix can't be less than 1 '" + tokens[i] + "'");
                        GENERAL_OP(opSwap, tmp, tmp + 1, 0);
                } catch (std::logic_error &) {
                    throw std::runtime_error("Failed to convert swap suffix '" + tokens[i] + "' to valid index");
                }
            }
        else if (tokens[i].length() == 1 && tokens[i][0] >= 'a' && tokens[i][0] <= 'z') {
            char srcChar = tokens[i][0];
            int loadIndex;
            if (srcChar >= 'x')
                loadIndex = srcChar - 'x';
            else
                loadIndex = srcChar - 'a' + 3;
            if (loadIndex >= numInputs)
                throw std::runtime_error("Too few input clips supplied to reference '" + tokens[i] + "'");
            LOAD_OP(getLoadOp(vi[loadIndex]), loadIndex, 0);
        } else {
            float f;
            std::string s;
            std::istringstream numStream(tokens[i]);
            numStream.imbue(std::locale::classic());
            if (!(numStream >> f))
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float");
            if (numStream >> s)
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float, not the whole token could be converted");
            LOAD_OP(opLoadConst, f, 0);
        }
    }

    if (tokens.size() > 0) {
        if (stackSize != 1)
            throw std::runtime_error("Stack unbalanced at end of expression. Need to have exactly one value on the stack to return.");
        ops.push_back(ExprOp(getStoreOp(outputVi), static_cast<float>((static_cast<int64_t>(1) << outputVi->format->bitsPerSample) - 1)));
    }

    return maxStackSize;
}

static float calculateOneOperand(uint32_t op, float a) {
    switch (op) {
        case opSqrt:
            return std::sqrt(a);
        case opAbs:
            return std::abs(a);
        case opNeg:
            return (a > 0) ? 0.0f : 1.0f;
        case opExp:
            return std::exp(a);
        case opLog:
            return std::log(a);
    }

    return 0.0f;
}

static float calculateTwoOperands(uint32_t op, float a, float b) {
    switch (op) {
        case opAdd:
            return a + b;
        case opSub:
            return a - b;
        case opMul:
            return a * b;
        case opDiv:
            return a / b;
        case opMax:
            return std::max(a, b);
        case opMin:
            return std::min(a, b);
        case opGt:
            return (a > b) ? 1.0f : 0.0f;
        case opLt:
            return (a < b) ? 1.0f : 0.0f;
        case opEq:
            return (a == b) ? 1.0f : 0.0f;
        case opLE:
            return (a <= b) ? 1.0f : 0.0f;
        case opGE:
            return (a >= b) ? 1.0f : 0.0f;
        case opAnd:
            return (a > 0 && b > 0) ? 1.0f : 0.0f;
        case opOr:
            return (a > 0 || b > 0) ? 1.0f : 0.0f;
        case opXor:
            return ((a > 0) != (b > 0)) ? 1.0f : 0.0f;
        case opPow:
            return std::pow(a, b);
    }

    return 0.0f;
}

static int numOperands(uint32_t op) {
    switch (op) {
        case opLoadConst:
        case opLoadSrc8:
        case opLoadSrc16:
        case opLoadSrcF32:
        case opLoadSrcF16:
        case opDup:
            return 0;

        case opSqrt:
        case opAbs:
        case opNeg:
        case opExp:
        case opLog:
            return 1;

        case opSwap:
        case opAdd:
        case opSub:
        case opMul:
        case opDiv:
        case opMax:
        case opMin:
        case opGt:
        case opLt:
        case opEq:
        case opLE:
        case opGE:
        case opAnd:
        case opOr:
        case opXor:
        case opPow:
            return 2;

        case opTernary:
            return 3;
    }

    return 0;
}

static bool isLoadOp(uint32_t op) {
    switch (op) {
        case opLoadConst:
        case opLoadSrc8:
        case opLoadSrc16:
        case opLoadSrcF32:
        case opLoadSrcF16:
            return true;
    }

    return false;
}

static void findBranches(std::vector<ExprOp> &ops, size_t pos, size_t *start1, size_t *start2, size_t *start3) {
    int operands = numOperands(ops[pos].op);

    size_t temp1, temp2, temp3;

    if (operands == 0) {
        *start1 = pos;
    } else if (operands == 1) {
        if (isLoadOp(ops[pos - 1].op)) {
            *start1 = pos - 1;
        } else {
            findBranches(ops, pos - 1, &temp1, &temp2, &temp3);
            *start1 = temp1;
        }
    } else if (operands == 2) {
        if (isLoadOp(ops[pos - 1].op)) {
            *start2 = pos - 1;
        } else {
            findBranches(ops, pos - 1, &temp1, &temp2, &temp3);
            *start2 = temp1;
        }

        if (isLoadOp(ops[*start2 - 1].op)) {
            *start1 = *start2 - 1;
        } else {
            findBranches(ops, *start2 - 1, &temp1, &temp2, &temp3);
            *start1 = temp1;
        }
    } else if (operands == 3) {
        if (isLoadOp(ops[pos - 1].op)) {
            *start3 = pos - 1;
        } else {
            findBranches(ops, pos - 1, &temp1, &temp2, &temp3);
            *start3 = temp1;
        }

        if (isLoadOp(ops[*start3 - 1].op)) {
            *start2 = *start3 - 1;
        } else {
            findBranches(ops, *start3 - 1, &temp1, &temp2, &temp3);
            *start2 = temp1;
        }

        if (isLoadOp(ops[*start2 - 1].op)) {
            *start1 = *start2 - 1;
        } else {
            findBranches(ops, *start2 - 1, &temp1, &temp2, &temp3);
            *start1 = temp1;
        }
    }
}

/*
#define PAIR(x) { x, #x }
static std::unordered_map<uint32_t, std::string> op_strings = {
        PAIR(opLoadSrc8),
        PAIR(opLoadSrc16),
        PAIR(opLoadSrcF32),
        PAIR(opLoadSrcF16),
        PAIR(opLoadConst),
        PAIR(opStore8),
        PAIR(opStore16),
        PAIR(opStoreF32),
        PAIR(opStoreF16),
        PAIR(opDup),
        PAIR(opSwap),
        PAIR(opAdd),
        PAIR(opSub),
        PAIR(opMul),
        PAIR(opDiv),
        PAIR(opMax),
        PAIR(opMin),
        PAIR(opSqrt),
        PAIR(opAbs),
        PAIR(opGt),
        PAIR(opLt),
        PAIR(opEq),
        PAIR(opLE),
        PAIR(opGE),
        PAIR(opTernary),
        PAIR(opAnd),
        PAIR(opOr),
        PAIR(opXor),
        PAIR(opNeg),
        PAIR(opExp),
        PAIR(opLog),
        PAIR(opPow)
    };
#undef PAIR


static void printExpression(const std::vector<ExprOp> &ops) {
    fprintf(stderr, "Expression: '");

    for (size_t i = 0; i < ops.size(); i++) {
        fprintf(stderr, " %s", op_strings[ops[i].op].c_str());

        if (ops[i].op == opLoadConst)
            fprintf(stderr, "(%.3f)", ops[i].e.fval);
        else if (isLoadOp(ops[i].op))
            fprintf(stderr, "(%d)", ops[i].e.ival);
    }

    fprintf(stderr, "'\n");
}
*/

static void foldConstants(std::vector<ExprOp> &ops) {
    for (size_t i = 0; i < ops.size(); i++) {
        switch (ops[i].op) {
            case opDup:
                if (ops[i - 1].op == opLoadConst && ops[i].e.ival == 0) {
                    ops[i] = ops[i - 1];
                }
                break;

            case opSqrt:
            case opAbs:
            case opNeg:
            case opExp:
            case opLog:
                if (ops[i - 1].op == opLoadConst) {
                    ops[i].e.fval = calculateOneOperand(ops[i].op, ops[i - 1].e.fval);
                    ops[i].op = opLoadConst;
                    ops.erase(ops.begin() + i - 1);
                    i--;
                }
                break;

            case opSwap:
                if (ops[i - 2].op == opLoadConst && ops[i - 1].op == opLoadConst && ops[i].e.ival == 1) {
                    const float temp = ops[i - 2].e.fval;
                    ops[i - 2].e.fval = ops[i - 1].e.fval;
                    ops[i - 1].e.fval = temp;
                    ops.erase(ops.begin() + i);
                    i--;
                }
                break;

            case opAdd:
            case opSub:
            case opMul:
            case opDiv:
            case opMax:
            case opMin:
            case opGt:
            case opLt:
            case opEq:
            case opLE:
            case opGE:
            case opAnd:
            case opOr:
            case opXor:
            case opPow:
                if (ops[i - 2].op == opLoadConst && ops[i - 1].op == opLoadConst) {
                    ops[i].e.fval = calculateTwoOperands(ops[i].op, ops[i - 2].e.fval, ops[i - 1].e.fval);
                    ops[i].op = opLoadConst;
                    ops.erase(ops.begin() + i - 2, ops.begin() + i);
                    i -= 2;
                }
                break;

            case opTernary:
                size_t start1, start2, start3;
                findBranches(ops, i, &start1, &start2, &start3);
                if (ops[start2 - 1].op == opLoadConst) {
                    ops.erase(ops.begin() + i);
                    if (ops[start1].e.fval > 0.0f) {
                        ops.erase(ops.begin() + start3, ops.begin() + i);
                        i = start3;
                    } else {
                        ops.erase(ops.begin() + start2, ops.begin() + start3);
                        i -= start3 - start2;
                    }
                    ops.erase(ops.begin() + start1);
                    i -= 2;
                }
                break;
        }
    }
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

        for (int i = 0; i < d->numInputs; i++)
            d->node[i] = vsapi->propGetNode(in, "clips", i, &err);

        const VSVideoInfo *vi[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < d->numInputs; i++)
            if (d->node[i])
                vi[i] = vsapi->getVideoInfo(d->node[i]);

        for (int i = 0; i < d->numInputs; i++) {
            if (!isConstantFormat(vi[i]))
                throw std::runtime_error("Only clips with constant format and dimensions allowed");
            if (vi[0]->format->numPlanes != vi[i]->format->numPlanes
                || vi[0]->format->subSamplingW != vi[i]->format->subSamplingW
                || vi[0]->format->subSamplingH != vi[i]->format->subSamplingH
                || vi[0]->width != vi[i]->width
                || vi[0]->height != vi[i]->height)
                throw std::runtime_error("All inputs must have the same number of planes and the same dimensions, subsampling included");

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
        for (int i = 0; i < nexpr; i++)
            expr[i] = vsapi->propGetData(in, "expr", i, nullptr);
        if (nexpr == 1) {
            expr[1] = expr[0];
            expr[2] = expr[0];
        } else if (nexpr == 2) {
            expr[2] = expr[1];
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
        }

        d->maxStackSize = 0;
        for (int i = 0; i < d->vi.format->numPlanes; i++) {
            d->maxStackSize = std::max(parseExpression(expr[i], d->ops[i], vi, &d->vi, d->numInputs), d->maxStackSize);
            foldConstants(d->ops[i]);
        }

        if (vs_get_cpulevel(core) > VS_CPU_LEVEL_NONE) {
            for (int i = 0; i < d->vi.format->numPlanes; i++) {
                if (d->plane[i] == poProcess) {
#ifdef VS_TARGET_CPU_X86
                    ExprCompiler compiler(d->numInputs);
                    for (auto op : d->ops[i]) {
                        compiler.add_op(op);
                    }

                    d->proc[i] = compiler.getCode();
                }
            }
#ifdef VS_TARGET_OS_WINDOWS
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
#endif
        }
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
