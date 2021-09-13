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

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "kernel/cpulevel.h"

#ifdef VS_TARGET_CPU_X86
#include <immintrin.h>
#include "expr/expr.h"
#include "expr/jitasm.h"
#ifndef VS_TARGET_OS_WINDOWS
#include <sys/mman.h>
#endif
#endif

using namespace expr;
using namespace vsh;

namespace {

enum PlaneOp {
    poProcess, poCopy, poUndefined
};

struct ExprData {
    VSNode *node[MAX_EXPR_INPUTS];
    VSVideoInfo vi;
    std::vector<ExprInstruction> bytecode[3];
    int plane[3];
    int numInputs;
    typedef void (*ProcessLineProc)(void *rwptrs, intptr_t ptroff[MAX_EXPR_INPUTS + 1], intptr_t niter);
    ProcessLineProc proc[3];
    size_t procSize[3];

    ExprData() : node(), vi(), plane(), numInputs(), proc() {}

    ~ExprData() {
#ifdef VS_TARGET_CPU_X86
        for (int i = 0; i < 3; i++) {
            if (proc[i]) {
#ifdef VS_TARGET_OS_WINDOWS
                VirtualFree((LPVOID)proc[i], 0, MEM_RELEASE);
#else
                munmap((void *)proc[i], procSize[i]);
#endif
            }
        }
#endif
    }
};

#ifdef VS_TARGET_CPU_X86
class ExprCompiler {
    virtual void load8(const ExprInstruction &insn) = 0;
    virtual void load16(const ExprInstruction &insn) = 0;
    virtual void loadF16(const ExprInstruction &insn) = 0;
    virtual void loadF32(const ExprInstruction &insn) = 0;
    virtual void loadConst(const ExprInstruction &insn) = 0;
    virtual void store8(const ExprInstruction &insn) = 0;
    virtual void store16(const ExprInstruction &insn) = 0;
    virtual void storeF16(const ExprInstruction &insn) = 0;
    virtual void storeF32(const ExprInstruction &insn) = 0;
    virtual void add(const ExprInstruction &insn) = 0;
    virtual void sub(const ExprInstruction &insn) = 0;
    virtual void mul(const ExprInstruction &insn) = 0;
    virtual void div(const ExprInstruction &insn) = 0;
    virtual void fma(const ExprInstruction &insn) = 0;
    virtual void max(const ExprInstruction &insn) = 0;
    virtual void min(const ExprInstruction &insn) = 0;
    virtual void sqrt(const ExprInstruction &insn) = 0;
    virtual void abs(const ExprInstruction &insn) = 0;
    virtual void neg(const ExprInstruction &insn) = 0;
    virtual void not_(const ExprInstruction &insn) = 0;
    virtual void and_(const ExprInstruction &insn) = 0;
    virtual void or_(const ExprInstruction &insn) = 0;
    virtual void xor_(const ExprInstruction &insn) = 0;
    virtual void cmp(const ExprInstruction &insn) = 0;
    virtual void ternary(const ExprInstruction &insn) = 0;
    virtual void exp(const ExprInstruction &insn) = 0;
    virtual void log(const ExprInstruction &insn) = 0;
    virtual void pow(const ExprInstruction &insn) = 0;
    virtual void sin(const ExprInstruction &insn) = 0;
    virtual void cos(const ExprInstruction &insn) = 0;
public:
    void addInstruction(const ExprInstruction &insn, VSCore *core, const VSAPI *vsapi)
    {
        switch (insn.op.type) {
        case ExprOpType::MEM_LOAD_U8: load8(insn); break;
        case ExprOpType::MEM_LOAD_U16: load16(insn); break;
        case ExprOpType::MEM_LOAD_F16: loadF16(insn); break;
        case ExprOpType::MEM_LOAD_F32: loadF32(insn); break;
        case ExprOpType::CONSTANT: loadConst(insn); break;
        case ExprOpType::MEM_STORE_U8: store8(insn); break;
        case ExprOpType::MEM_STORE_U16: store16(insn); break;
        case ExprOpType::MEM_STORE_F16: storeF16(insn); break;
        case ExprOpType::MEM_STORE_F32: storeF32(insn); break;
        case ExprOpType::ADD: add(insn); break;
        case ExprOpType::SUB: sub(insn); break;
        case ExprOpType::MUL: mul(insn); break;
        case ExprOpType::DIV: div(insn); break;
        case ExprOpType::FMA: fma(insn); break;
        case ExprOpType::MAX: max(insn); break;
        case ExprOpType::MIN: min(insn); break;
        case ExprOpType::SQRT: sqrt(insn); break;
        case ExprOpType::ABS: abs(insn); break;
        case ExprOpType::NEG: neg(insn); break;
        case ExprOpType::NOT: not_(insn); break;
        case ExprOpType::AND: and_(insn); break;
        case ExprOpType::OR: or_(insn); break;
        case ExprOpType::XOR: xor_(insn); break;
        case ExprOpType::CMP: cmp(insn); break;
        case ExprOpType::TERNARY: ternary(insn); break;
        case ExprOpType::EXP: exp(insn); break;
        case ExprOpType::LOG: log(insn); break;
        case ExprOpType::POW: pow(insn); break;
        case ExprOpType::SIN: sin(insn); break;
        case ExprOpType::COS: cos(insn); break;
        default: vsapi->logMessage(mtFatal, "illegal opcode", core); break;
        }
    }

    virtual ~ExprCompiler() {}
    virtual std::pair<ExprData::ProcessLineProc, size_t> getCode() = 0;
};

class ExprCompiler128 : public ExprCompiler, private jitasm::function<void, ExprCompiler128, uint8_t *, const intptr_t *, intptr_t> {
    typedef jitasm::function<void, ExprCompiler128, uint8_t *, const intptr_t *, intptr_t> jit;
    friend struct jitasm::function<void, ExprCompiler128, uint8_t *, const intptr_t *, intptr_t>;
    friend struct jitasm::function_cdecl<void, ExprCompiler128, uint8_t *, const intptr_t *, intptr_t>;

#define SPLAT(x) { (x), (x), (x), (x) }
    static constexpr ExprUnion constData alignas(16)[53][4] = {
        SPLAT(0x7FFFFFFF), // absmask
        SPLAT(0x80000000), // negmask
        SPLAT(0x7F), // x7F
        SPLAT(0x00800000), // min_norm_pos
        SPLAT(~0x7F800000), // inv_mant_mask
        SPLAT(1.0f), // float_one
        SPLAT(0.5f), // float_half
        SPLAT(255.0f), // float_255
        SPLAT(511.0f), // float_511
        SPLAT(1023.0f), // float_1023
        SPLAT(2047.0f), // float_2047
        SPLAT(4095.0f), // float_4095
        SPLAT(8191.0f), // float_8191
        SPLAT(16383.0f), // float_16383
        SPLAT(32767.0f), // float_32767
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
        SPLAT(+3.3333331174E-1f), // log_p8
        SPLAT(0x3ea2f983), // float_invpi, 1/pi
        SPLAT(0x4b400000), // float_rintf
        SPLAT(0x40490000), // float_pi1
        SPLAT(0x3a7da000), // float_pi2
        SPLAT(0x34222000), // float_pi3
        SPLAT(0x2cb4611a), // float_pi4
        SPLAT(0xbe2aaaa6), // float_sinC3
        SPLAT(0x3c08876a), // float_sinC5
        SPLAT(0xb94fb7ff), // float_sinC7
        SPLAT(0x362edef8), // float_sinC9
        SPLAT(static_cast<int32_t>(0xBEFFFFE2)), // float_cosC2
        SPLAT(0x3D2AA73C), // float_cosC4
        SPLAT(static_cast<int32_t>(0XBAB58D50)), // float_cosC6
        SPLAT(0x37C1AD76), // float_cosC8
    };

    struct ConstantIndex {
        static constexpr int absmask = 0;
        static constexpr int negmask = 1;
        static constexpr int x7F = 2;
        static constexpr int min_norm_pos = 3;
        static constexpr int inv_mant_mask = 4;
        static constexpr int float_one = 5;
        static constexpr int float_half = 6;
        static constexpr int float_255 = 7;
        static constexpr int float_511 = 8;
        static constexpr int float_1023 = 9;
        static constexpr int float_2047 = 10;
        static constexpr int float_4095 = 11;
        static constexpr int float_8191 = 12;
        static constexpr int float_16383 = 13;
        static constexpr int float_32767 = 14;
        static constexpr int float_65535 = 15;
        static constexpr int i16min_epi16 = 16;
        static constexpr int i16min_epi32 = 17;
        static constexpr int exp_hi = 18;
        static constexpr int exp_lo = 19;
        static constexpr int log2e = 20;
        static constexpr int exp_c1 = 21;
        static constexpr int exp_c2 = 22;
        static constexpr int exp_p0 = 23;
        static constexpr int exp_p1 = 24;
        static constexpr int exp_p2 = 25;
        static constexpr int exp_p3 = 26;
        static constexpr int exp_p4 = 27;
        static constexpr int exp_p5 = 28;
        static constexpr int sqrt_1_2 = 29;
        static constexpr int log_p0 = 30;
        static constexpr int log_p1 = 31;
        static constexpr int log_p2 = 32;
        static constexpr int log_p3 = 33;
        static constexpr int log_p4 = 34;
        static constexpr int log_p5 = 35;
        static constexpr int log_p6 = 36;
        static constexpr int log_p7 = 37;
        static constexpr int log_p8 = 38;
        static constexpr int log_q1 = exp_c2;
        static constexpr int log_q2 = exp_c1;
        static constexpr int float_invpi = 39;
        static constexpr int float_rintf = 40;
        static constexpr int float_pi1 = 41;
        static constexpr int float_pi2 = float_pi1 + 1;
        static constexpr int float_pi3 = float_pi1 + 2;
        static constexpr int float_pi4 = float_pi1 + 3;
        static constexpr int float_sinC3 = 45;
        static constexpr int float_sinC5 = float_sinC3 + 1;
        static constexpr int float_sinC7 = float_sinC3 + 2;
        static constexpr int float_sinC9 = float_sinC3 + 3;
        static constexpr int float_cosC2 = 49;
        static constexpr int float_cosC4 = float_cosC2 + 1;
        static constexpr int float_cosC6 = float_cosC2 + 2;
        static constexpr int float_cosC8 = float_cosC2 + 3;
    };
#undef SPLAT

    // JitASM compiles everything from main(), so record the operations for later.
    std::vector<std::function<void(Reg, XmmReg, Reg, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &)>> deferred;

    CPUFeatures cpuFeatures;
    int numInputs;
    int curLabel;

#define EMIT() [this, insn](Reg regptrs, XmmReg zero, Reg constants, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &bytecodeRegs)
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

    void load8(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            VEX1(movq, t1.first, mmword_ptr[a]);
            VEX2(punpcklbw, t1.first, t1.first, zero);
            VEX2(punpckhwd, t1.second, t1.first, zero);
            VEX2(punpcklwd, t1.first, t1.first, zero);
            VEX1(cvtdq2ps, t1.first, t1.first);
            VEX1(cvtdq2ps, t1.second, t1.second);
        });
    }

    void load16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            VEX1(movdqa, t1.first, xmmword_ptr[a]);
            VEX2(punpckhwd, t1.second, t1.first, zero);
            VEX2(punpcklwd, t1.first, t1.first, zero);
            VEX1(cvtdq2ps, t1.first, t1.first);
            VEX1(cvtdq2ps, t1.second, t1.second);
        });
    }

    void loadF16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            vcvtph2ps(t1.first, qword_ptr[a]);
            vcvtph2ps(t1.second, qword_ptr[a + 8]);
        });
    }

    void loadF32(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            VEX1(movdqa, t1.first, xmmword_ptr[a]);
            VEX1(movdqa, t1.second, xmmword_ptr[a + 16]);
        });
    }

    void loadConst(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];

            if (insn.op.imm.f == 0.0f) {
                VEX1(movaps, t1.first, zero);
                VEX1(movaps, t1.second, zero);
                return;
            }

            Reg32 a;
            mov(a, insn.op.imm.u);
            VEX1(movd, t1.first, a);
            VEX2IMM(shufps, t1.first, t1.first, t1.first, 0);
            VEX1(movaps, t1.second, t1.first);
        });
    }

    void store8(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            XmmReg r1, r2, limit;
            Reg a;
            VEX1(movaps, limit, xmmword_ptr[constants + ConstantIndex::float_255 * 16]);
            VEX2(minps, r1, t1.first, limit);
            VEX2(minps, r2, t1.second, limit);
            VEX1(cvtps2dq, r1, r1);
            VEX1(cvtps2dq, r2, r2);
            VEX2(packssdw, r1, r1, r2);
            VEX2(packuswb, r1, r1, zero);
            mov(a, ptr[regptrs]);
            VEX1(movq, mmword_ptr[a], r1);
        });
    }

    void store16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            int depth = insn.op.imm.u;
            auto t1 = bytecodeRegs[insn.src1];
            XmmReg r1, r2, limit;
            Reg a;
            VEX1(movaps, limit, xmmword_ptr[constants + (ConstantIndex::float_255 + depth - 8) * 16]);
            VEX2IMM(shufps, limit, limit, limit, 0);
            VEX2(minps, r1, t1.first, limit);
            VEX2(minps, r2, t1.second, limit);
            VEX1(cvtps2dq, r1, r1);
            VEX1(cvtps2dq, r2, r2);

            if (cpuFeatures.sse4_1) {
                VEX2(packusdw, r1, r1, r2);
            } else {
                if (depth >= 16) {
                    VEX1(movaps, limit, xmmword_ptr[constants + ConstantIndex::i16min_epi32 * 16]);
                    VEX2(paddd, r1, r1, limit);
                    VEX2(paddd, r2, r2, limit);
                }
                VEX2(packssdw, r1, r1, r2);
                if (depth >= 16)
                    VEX2(psubw, r1, r1, xmmword_ptr[constants + ConstantIndex::i16min_epi16 * 16]);
            }
            mov(a, ptr[regptrs]);
            VEX1(movaps, xmmword_ptr[a], r1);
        });
    }

    void storeF16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];

            Reg a;
            mov(a, ptr[regptrs]);
            vcvtps2ph(qword_ptr[a], t1.first, 0);
            vcvtps2ph(qword_ptr[a + 8], t1.second, 0);
        });
    }

    void storeF32(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];

            Reg a;
            mov(a, ptr[regptrs]);
            VEX1(movaps, xmmword_ptr[a], t1.first);
            VEX1(movaps, xmmword_ptr[a + 16], t1.second);
        });
    }

#define BINARYOP(op) \
do { \
  auto t1 = bytecodeRegs[insn.src1]; \
  auto t2 = bytecodeRegs[insn.src2]; \
  auto t3 = bytecodeRegs[insn.dst]; \
  VEX2(op, t3.first, t1.first, t2.first); \
  VEX2(op, t3.second, t1.second, t2.second); \
} while (0)
    void add(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(addps);
        });
    }

    void sub(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(subps);
        });
    }

    void mul(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(mulps);
        });
    }

    void div(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(divps);
        });
    }

    void fma(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            FMAType type = static_cast<FMAType>(insn.op.imm.u);

            // t1 + t2 * t3
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.src3];
            auto t4 = bytecodeRegs[insn.dst];

            if (cpuFeatures.fma3) {
#define FMA3(op) \
do { \
  if (insn.dst == insn.src1) { \
    v##op##231ps(t1.first, t2.first, t3.first); \
    v##op##231ps(t1.second, t2.second, t3.second); \
  } else if (insn.dst == insn.src2) { \
    v##op##132ps(t2.first, t1.first, t3.first); \
    v##op##132ps(t2.second, t1.second, t3.second); \
  } else if (insn.dst == insn.src3) { \
    v##op##132ps(t3.first, t1.first, t2.first); \
    v##op##132ps(t3.second, t1.second, t2.second); \
  } else { \
    vmovaps(t4.first, t1.first); \
    vmovaps(t4.second, t1.second); \
    v##op##231ps(t4.first, t2.first, t3.first); \
    v##op##231ps(t4.second, t2.second, t3.second); \
  } \
} while (0)
                switch (type) {
                case FMAType::FMADD: FMA3(fmadd); break;
                case FMAType::FMSUB: FMA3(fmsub); break;
                case FMAType::FNMADD: FMA3(fnmadd); break;
                case FMAType::FNMSUB: FMA3(fnmsub); break;
                }
#undef FMA3
            } else {
                XmmReg r1, r2;
                VEX2(mulps, r1, t2.first, t3.first);
                VEX2(mulps, r2, t2.second, t3.second);

                if (type == FMAType::FMADD || type == FMAType::FNMSUB) {
                    VEX2(addps, t4.first, r1, t1.first);
                    VEX2(addps, t4.second, r2, t1.second);
                } else if (type == FMAType::FMSUB) {
                    VEX2(subps, t4.first, r1, t1.first);
                    VEX2(subps, t4.second, r2, t1.second);
                } else if (type == FMAType::FNMADD) {
                    VEX2(subps, t4.first, t1.first, r1);
                    VEX2(subps, t4.second, t1.second, r2);
                }

                if (type == FMAType::FNMSUB) {
                    VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::negmask * 16]);
                    VEX2(xorps, t4.first, t4.first, r1);
                    VEX2(xorps, t4.second, t4.second, r2);
                }
            }
        });
    }

    void max(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(maxps);
        });
    }

    void min(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(minps);
        });
    }
#undef BINARYOP

    void sqrt(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            VEX2(maxps, t2.first, t1.first, zero);
            VEX2(maxps, t2.second, t1.second, zero);
            VEX1(sqrtps, t2.first, t2.first);
            VEX1(sqrtps, t2.second, t2.second);
        });
    }

    void abs(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::absmask * 16]);
            VEX2(andps, t2.first, t1.first, r1);
            VEX2(andps, t2.second, t1.second, r1);
        });
    }

    void neg(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::negmask * 16]);
            VEX2(xorps, t2.first, t1.first, r1);
            VEX2(xorps, t2.second, t1.second, r1);
        });
    }

    void not_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            VEX2IMM(cmpps, t2.first, t1.first, zero, _CMP_LE_OS);
            VEX2IMM(cmpps, t2.second, t1.second, zero, _CMP_LE_OS);
            VEX2(andps, t2.first, t2.first, r1);
            VEX2(andps, t2.second, t2.second, r1);
        });
    }

#define LOGICOP(op) \
do { \
  auto t1 = bytecodeRegs[insn.src1]; \
  auto t2 = bytecodeRegs[insn.src2]; \
  auto t3 = bytecodeRegs[insn.dst]; \
  XmmReg r1, tmp1, tmp2; \
  VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]); \
  VEX2IMM(cmpps, tmp1, t1.first, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, tmp2, t1.second, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, t3.first, t2.first, zero, _CMP_NLE_US); \
  VEX2IMM(cmpps, t3.second, t2.second, zero, _CMP_NLE_US); \
  VEX2(op, t3.first, t3.first, tmp1); \
  VEX2(op, t3.second, t3.second, tmp2); \
  VEX2(andps, t3.first, t3.first, r1); \
  VEX2(andps, t3.second, t3.second, r1); \
} while (0)

    void and_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(andps);
        });
    }

    void or_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(orps);
        });
    }

    void xor_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(xorps);
        });
    }
#undef LOGICOP

    void cmp(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.dst];
            XmmReg r1;
            VEX1(movaps, r1, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            VEX2IMM(cmpps, t3.first, t1.first, t2.first, insn.op.imm.u);
            VEX2IMM(cmpps, t3.second, t1.second, t2.second, insn.op.imm.u);
            VEX2(andps, t3.first, t3.first, r1);
            VEX2(andps, t3.second, t3.second, r1);
        });
    }

    void ternary(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.src3];
            auto t4 = bytecodeRegs[insn.dst];

            XmmReg r1, r2;
            VEX2IMM(cmpps, r1, t1.first, zero, _CMP_NLE_US);
            VEX2IMM(cmpps, r2, t1.second, zero, _CMP_NLE_US);

            if (cpuFeatures.sse4_1) {
                VEX2IMM(blendvps, t4.first, t3.first, t2.first, r1);
                VEX2IMM(blendvps, t4.second, t3.second, t2.second, r2);
            } else {
                VEX2(andps, t4.first, t3.first, r1);
                VEX2(andps, t4.second, t3.second, r2);
                VEX2(andnps, r1, r1, t2.first);
                VEX2(andnps, r2, r2, t2.second);
                VEX2(orps, t4.first, t4.first, r1);
                VEX2(orps, t4.second, t4.second, r2);
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
    }

    void exp(const ExprInstruction &insn) override
    {
        int l = curLabel++;

        deferred.push_back([this, insn, l](Reg regptrs, XmmReg zero, Reg constants, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &bytecodeRegs)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            XmmReg r1, r2, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            exp_(r1, one, constants);
            VEX1(movaps, t2.first, t2.second);
            VEX1(movaps, t2.second, r1);
            VEX1(movaps, r1, r2);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void log(const ExprInstruction &insn) override
    {
        int l = curLabel++;

        deferred.push_back([this, insn, l](Reg regptrs, XmmReg zero, Reg constants, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &bytecodeRegs)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            XmmReg r1, r2, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            log_(r1, zero, one, constants);
            VEX1(movaps, t2.first, t2.second);
            VEX1(movaps, t2.second, r1);
            VEX1(movaps, r1, r2);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void pow(const ExprInstruction &insn) override
    {
        int l = curLabel++;

        deferred.push_back([this, insn, l](Reg regptrs, XmmReg zero, Reg constants, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &bytecodeRegs)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.dst];

            XmmReg r1, r2, r3, r4, one;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);
            VEX1(movaps, r3, t2.first);
            VEX1(movaps, r4, t2.second);
            VEX1(movaps, one, xmmword_ptr[constants + ConstantIndex::float_one * 16]);

            L(label);

            log_(r1, zero, one, constants);
            VEX2(mulps, r1, r1, r3);
            exp_(r1, one, constants);

            VEX1(movaps, t3.first, t3.second);
            VEX1(movaps, t3.second, r1);
            VEX1(movaps, r1, r2);
            VEX1(movaps, r3, r4);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void sincos_(bool issin, XmmReg y, XmmReg x, Reg constants)
    {
        XmmReg t1, sign, t2, t3, t4;
        // Remove sign
        VEX1(movaps, t1, xmmword_ptr[constants + ConstantIndex::absmask * 16]);
        if (issin) {
            VEX1(movaps, sign, t1);
            VEX2(andnps, sign, sign, x);
        } else {
            VEX2(pxor, sign, sign, sign);
        }
        VEX2(andps, t1, t1, x);
        // Range reduction
        VEX1(movaps, t3, xmmword_ptr[constants + ConstantIndex::float_rintf * 16]);
        VEX2(mulps, t2, t1, xmmword_ptr[constants + ConstantIndex::float_invpi * 16]);
        VEX2(addps, t2, t2, t3);
        VEX1IMM(pslld, t4, t2, 31);
        VEX2(xorps, sign, sign, t4);
        VEX2(subps, t2, t2, t3);
        if (cpuFeatures.fma3) {
            vfnmadd231ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_pi1 * 16]);
            vfnmadd231ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_pi2 * 16]);
            vfnmadd231ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_pi3 * 16]);
            vfnmadd231ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_pi4 * 16]);
        } else {
            VEX2(mulps, t4, t2, xmmword_ptr[constants + ConstantIndex::float_pi1 * 16]);
            VEX2(subps, t1, t1, t4);
            VEX2(mulps, t4, t2, xmmword_ptr[constants + ConstantIndex::float_pi2 * 16]);
            VEX2(subps, t1, t1, t4);
            VEX2(mulps, t4, t2, xmmword_ptr[constants + ConstantIndex::float_pi3 * 16]);
            VEX2(subps, t1, t1, t4);
            VEX2(mulps, t4, t2, xmmword_ptr[constants + ConstantIndex::float_pi4 * 16]);
            VEX2(subps, t1, t1, t4);
        }
        if (issin) {
            // Evaluate minimax polynomial for sin(x) in [-pi/2, pi/2] interval
            // Y <- X + X * X^2 * (C3 + X^2 * (C5 + X^2 * (C7 + X^2 * C9)))
            VEX2(mulps, t2, t1, t1);
            if (cpuFeatures.fma3) {
                vmovaps(t3, xmmword_ptr[constants + ConstantIndex::float_sinC7 * 16]);
                vfmadd231ps(t3, t2, xmmword_ptr[constants + ConstantIndex::float_sinC9 * 16]);
                vfmadd213ps(t3, t2, xmmword_ptr[constants + ConstantIndex::float_sinC5 * 16]);
                vfmadd213ps(t3, t2, xmmword_ptr[constants + ConstantIndex::float_sinC3 * 16]);
                VEX2(mulps, t3, t3, t2);
                vfmadd231ps(t1, t1, t3);
            } else {
                VEX2(mulps, t3, t2, xmmword_ptr[constants + ConstantIndex::float_sinC9 * 16]);
                VEX2(addps, t3, t3, xmmword_ptr[constants + ConstantIndex::float_sinC7 * 16]);
                VEX2(mulps, t3, t3, t2);
                VEX2(addps, t3, t3, xmmword_ptr[constants + ConstantIndex::float_sinC5 * 16]);
                VEX2(mulps, t3, t3, t2);
                VEX2(addps, t3, t3, xmmword_ptr[constants + ConstantIndex::float_sinC3 * 16]);
                VEX2(mulps, t3, t3, t2);
                VEX2(mulps, t3, t3, t1);
                VEX2(addps, t1, t1, t3);
            }
        } else {
            // Evaluate minimax polynomial for cos(x) in [-pi/2, pi/2] interval
            // Y <- 1 + X^2 * (C2 + X^2 * (C4 + X^2 * (C6 + X^2 * C8)))
            VEX2(mulps, t2, t1, t1);
            if (cpuFeatures.fma3) {
                vmovaps(t1, xmmword_ptr[constants + ConstantIndex::float_cosC6 * 16]);
                vfmadd231ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_cosC8 * 16]);
                vfmadd213ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_cosC4 * 16]);
                vfmadd213ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_cosC2 * 16]);
                vfmadd213ps(t1, t2, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            } else {
                VEX2(mulps, t1, t2, xmmword_ptr[constants + ConstantIndex::float_cosC8 * 16]);
                VEX2(addps, t1, t1, xmmword_ptr[constants + ConstantIndex::float_cosC6 * 16]);
                VEX2(mulps, t1, t1, t2);
                VEX2(addps, t1, t1, xmmword_ptr[constants + ConstantIndex::float_cosC4 * 16]);
                VEX2(mulps, t1, t1, t2);
                VEX2(addps, t1, t1, xmmword_ptr[constants + ConstantIndex::float_cosC2 * 16]);
                VEX2(mulps, t1, t1, t2);
                VEX2(addps, t1, t1, xmmword_ptr[constants + ConstantIndex::float_one * 16]);
            }
        }
        // Apply sign
        VEX2(xorps, y, t1, sign);
    }

    void sincos(bool issin, const ExprInstruction &insn)
    {
        int l = curLabel++;

        deferred.push_back([this, issin, insn, l](Reg regptrs, XmmReg zero, Reg constants, std::unordered_map<int, std::pair<XmmReg, XmmReg>> &bytecodeRegs)
        {
            char label[] = "label-0000";
            sprintf(label, "label-%04d", l);

            auto t1 = bytecodeRegs[insn.src1];
            auto t3 = bytecodeRegs[insn.dst];

            XmmReg r1, r2;
            Reg a;
            mov(a, 2);
            VEX1(movaps, r1, t1.first);
            VEX1(movaps, r2, t1.second);

            L(label);

            sincos_(issin, r1, r1, constants);
            VEX1(movaps, t3.first, t3.second);
            VEX1(movaps, t3.second, r1);
            VEX1(movaps, r1, r2);

            jit::sub(a, 1);
            jnz(label);
        });
    }

    void sin(const ExprInstruction &insn) override
    {
        sincos(true, insn);
    }

    void cos(const ExprInstruction &insn) override
    {
        sincos(false, insn);
    }

    void main(Reg regptrs, Reg regoffs, Reg niter)
    {
        std::unordered_map<int, std::pair<XmmReg, XmmReg>> bytecodeRegs;
        XmmReg zero;
        VEX2(pxor, zero, zero, zero);
        Reg constants;
        mov(constants, (uintptr_t)constData);

        L("wloop");

        for (const auto &f : deferred) {
            f(regptrs, zero, constants, bytecodeRegs);
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
    explicit ExprCompiler128(int numInputs) : cpuFeatures(*getCPUFeatures()), numInputs(numInputs), curLabel() {}

    std::pair<ExprData::ProcessLineProc, size_t> getCode() override
    {
        size_t size;
        if (jit::GetCode() && (size = GetCodeSize())) {
#ifdef VS_TARGET_OS_WINDOWS
            void *ptr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
            void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#endif
            memcpy(ptr, jit::GetCode(), size);
            return {reinterpret_cast<ExprData::ProcessLineProc>(ptr), size};
        }
        return {nullptr, 0};
    }
#undef VEX2IMM
#undef VEX2
#undef VEX1IMM
#undef VEX1
#undef EMIT
};

constexpr ExprUnion ExprCompiler128::constData alignas(16)[53][4];

class ExprCompiler256 : public ExprCompiler, private jitasm::function<void, ExprCompiler256, uint8_t *, const intptr_t *, intptr_t> {
    typedef jitasm::function<void, ExprCompiler256, uint8_t *, const intptr_t *, intptr_t> jit;
    friend struct jitasm::function<void, ExprCompiler256, uint8_t *, const intptr_t *, intptr_t>;
    friend struct jitasm::function_cdecl<void, ExprCompiler256, uint8_t *, const intptr_t *, intptr_t>;

#define SPLAT(x) { (x), (x), (x), (x), (x), (x), (x), (x) }
    static constexpr ExprUnion constData alignas(32)[53][8] = {
        SPLAT(0x7FFFFFFF), // absmask
        SPLAT(0x80000000), // negmask
        SPLAT(0x7F), // x7F
        SPLAT(0x00800000), // min_norm_pos
        SPLAT(~0x7F800000), // inv_mant_mask
        SPLAT(1.0f), // float_one
        SPLAT(0.5f), // float_half
        SPLAT(255.0f), // float_255
        SPLAT(511.0f), // float_511
        SPLAT(1023.0f), // float_1023
        SPLAT(2047.0f), // float_2047
        SPLAT(4095.0f), // float_4095
        SPLAT(8191.0f), // float_8191
        SPLAT(16383.0f), // float_16383
        SPLAT(32767.0f), // float_32767
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
        SPLAT(+3.3333331174E-1f), // log_p8
        SPLAT(0x3ea2f983), // float_invpi, 1/pi
        SPLAT(0x4b400000), // float_rintf
        SPLAT(0x40490000), // float_pi1
        SPLAT(0x3a7da000), // float_pi2
        SPLAT(0x34222000), // float_pi3
        SPLAT(0x2cb4611a), // float_pi4
        SPLAT(0xbe2aaaa6), // float_sinC3
        SPLAT(0x3c08876a), // float_sinC5
        SPLAT(0xb94fb7ff), // float_sinC7
        SPLAT(0x362edef8), // float_sinC9
        SPLAT(static_cast<int32_t>(0xBEFFFFE2)), // float_cosC2
        SPLAT(0x3D2AA73C), // float_cosC4
        SPLAT(static_cast<int32_t>(0XBAB58D50)), // float_cosC6
        SPLAT(0x37C1AD76), // float_cosC8
    };

    struct ConstantIndex {
        static constexpr int absmask = 0;
        static constexpr int negmask = 1;
        static constexpr int x7F = 2;
        static constexpr int min_norm_pos = 3;
        static constexpr int inv_mant_mask = 4;
        static constexpr int float_one = 5;
        static constexpr int float_half = 6;
        static constexpr int float_255 = 7;
        static constexpr int float_511 = 8;
        static constexpr int float_1023 = 9;
        static constexpr int float_2047 = 10;
        static constexpr int float_4095 = 11;
        static constexpr int float_8191 = 12;
        static constexpr int float_16383 = 13;
        static constexpr int float_32767 = 14;
        static constexpr int float_65535 = 15;
        static constexpr int i16min_epi16 = 16;
        static constexpr int i16min_epi32 = 17;
        static constexpr int exp_hi = 18;
        static constexpr int exp_lo = 19;
        static constexpr int log2e = 20;
        static constexpr int exp_c1 = 21;
        static constexpr int exp_c2 = 22;
        static constexpr int exp_p0 = 23;
        static constexpr int exp_p1 = 24;
        static constexpr int exp_p2 = 25;
        static constexpr int exp_p3 = 26;
        static constexpr int exp_p4 = 27;
        static constexpr int exp_p5 = 28;
        static constexpr int sqrt_1_2 = 29;
        static constexpr int log_p0 = 30;
        static constexpr int log_p1 = 31;
        static constexpr int log_p2 = 32;
        static constexpr int log_p3 = 33;
        static constexpr int log_p4 = 34;
        static constexpr int log_p5 = 35;
        static constexpr int log_p6 = 36;
        static constexpr int log_p7 = 37;
        static constexpr int log_p8 = 38;
        static constexpr int log_q1 = exp_c2;
        static constexpr int log_q2 = exp_c1;
        static constexpr int float_invpi = 39;
        static constexpr int float_rintf = 40;
        static constexpr int float_pi1 = 41;
        static constexpr int float_pi2 = float_pi1 + 1;
        static constexpr int float_pi3 = float_pi1 + 2;
        static constexpr int float_pi4 = float_pi1 + 3;
        static constexpr int float_sinC3 = 45;
        static constexpr int float_sinC5 = float_sinC3 + 1;
        static constexpr int float_sinC7 = float_sinC3 + 2;
        static constexpr int float_sinC9 = float_sinC3 + 3;
        static constexpr int float_cosC2 = 49;
        static constexpr int float_cosC4 = float_cosC2 + 1;
        static constexpr int float_cosC6 = float_cosC2 + 2;
        static constexpr int float_cosC8 = float_cosC2 + 3;
    };
#undef SPLAT

    // JitASM compiles everything from main(), so record the operations for later.
    std::vector<std::function<void(Reg, YmmReg, Reg, std::unordered_map<int, YmmReg> &)>> deferred;

    CPUFeatures cpuFeatures;
    int numInputs;
    int curLabel;

#define EMIT() [this, insn](Reg regptrs, YmmReg zero, Reg constants, std::unordered_map<int, YmmReg> &bytecodeRegs)

    void load8(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            vpmovzxbd(t1, mmword_ptr[a]);
            vcvtdq2ps(t1, t1);
        });
    }

    void load16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            vpmovzxwd(t1, xmmword_ptr[a]);
            vcvtdq2ps(t1, t1);
        });
    }

    void loadF16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            vcvtph2ps(t1, xmmword_ptr[a]);
        });
    }

    void loadF32(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];
            Reg a;
            mov(a, ptr[regptrs + sizeof(void *) * (insn.op.imm.u + 1)]);
            vmovaps(t1, ymmword_ptr[a]);
        });
    }

    void loadConst(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.dst];

            if (insn.op.imm.f == 0.0f) {
                vmovaps(t1, zero);
                return;
            }

            XmmReg r1;
            Reg32 a;
            mov(a, insn.op.imm.u);
            vmovd(r1, a);
            vbroadcastss(t1, r1);
        });
    }

    void store8(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            YmmReg r1;
            Reg a;
            vminps(r1, t1, ymmword_ptr[constants + ConstantIndex::float_255 * 32]);
            vcvtps2dq(r1, r1);
            vpackssdw(r1, r1, r1);
            vpermq(r1, r1, 0x08);
            vpackuswb(r1, r1, zero);
            mov(a, ptr[regptrs]);
            vmovq(qword_ptr[a], r1.as128());
        });
    }

    void store16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            int depth = insn.op.imm.u;
            auto t1 = bytecodeRegs[insn.src1];
            YmmReg r1, limit;
            Reg a;
            vminps(r1, t1, ymmword_ptr[constants + (ConstantIndex::float_255 + depth - 8) * 32]);
            vcvtps2dq(r1, r1);
            vpackusdw(r1, r1, r1);
            vpermq(r1, r1, 0x08);
            mov(a, ptr[regptrs]);
            vmovaps(xmmword_ptr[a], r1.as128());
        });
    }

    void storeF16(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            Reg a;
            mov(a, ptr[regptrs]);
            vcvtps2ph(xmmword_ptr[a], t1, 0);
        });
    }

    void storeF32(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            Reg a;
            mov(a, ptr[regptrs]);
            vmovaps(ymmword_ptr[a], t1);
        });
    }

#define BINARYOP(op) \
do { \
  auto t1 = bytecodeRegs[insn.src1]; \
  auto t2 = bytecodeRegs[insn.src2]; \
  auto t3 = bytecodeRegs[insn.dst]; \
  op(t3, t1, t2); \
} while (0)
    void add(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vaddps);
        });
    }

    void sub(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vsubps);
        });
    }

    void mul(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vmulps);
        });
    }

    void div(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vdivps);
        });
    }

    void fma(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            FMAType type = static_cast<FMAType>(insn.op.imm.u);

            // t1 + t2 * t3
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.src3];
            auto t4 = bytecodeRegs[insn.dst];

#define FMA3(op) \
do { \
  if (insn.dst == insn.src1) { \
    op##231ps(t1, t2, t3); \
  } else if (insn.dst == insn.src2) { \
    op##132ps(t2, t1, t3); \
  } else if (insn.dst == insn.src3) { \
    op##132ps(t3, t1, t2); \
  } else { \
    vmovaps(t4, t1); \
    op##231ps(t4, t2, t3); \
  } \
} while (0)
            switch (type) {
            case FMAType::FMADD: FMA3(vfmadd); break;
            case FMAType::FMSUB: FMA3(vfmsub); break;
            case FMAType::FNMADD: FMA3(vfnmadd); break;
            case FMAType::FNMSUB: FMA3(vfnmsub); break;
            }
#undef FMA3
        });
    }

    void max(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vmaxps);
        });
    }

    void min(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            BINARYOP(vminps);
        });
    }
#undef BINARYOP

    void sqrt(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            vmaxps(t2, t1, zero);
            vsqrtps(t2, t2);
        });
    }

    void abs(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            vandps(t2, t1, ymmword_ptr[constants + ConstantIndex::absmask * 32]);
        });
    }

    void neg(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            vxorps(t2, t1, ymmword_ptr[constants + ConstantIndex::negmask * 32]);
        });
    }

    void not_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            YmmReg r1;
            vcmpps(t2, t1, zero, _CMP_LE_OS);
            vandps(t2, t2, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
        });
    }

#define LOGICOP(op) \
do { \
  auto t1 = bytecodeRegs[insn.src1]; \
  auto t2 = bytecodeRegs[insn.src2]; \
  auto t3 = bytecodeRegs[insn.dst]; \
  YmmReg tmp; \
  vcmpps(tmp, t1, zero, _CMP_NLE_US); \
  vcmpps(t3, t2, zero, _CMP_NLE_US); \
  op(t3, t3, tmp); \
  vandps(t3, t3, ymmword_ptr[constants + ConstantIndex::float_one * 32]); \
} while (0)

    void and_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(vandps);
        });
    }

    void or_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(vorps);
        });
    }

    void xor_(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            LOGICOP(vxorps);
        });
    }
#undef LOGICOP

    void cmp(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.dst];
            vcmpps(t3, t1, t2, insn.op.imm.u);
            vandps(t3, t3, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
        });
    }

    void ternary(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.src3];
            auto t4 = bytecodeRegs[insn.dst];
            YmmReg r1;
            vcmpps(r1, t1, zero, _CMP_NLE_US);
            vblendvps(t4, t3, t2, r1);
        });
    }

    void exp_(YmmReg x, YmmReg one, Reg constants)
    {
        YmmReg fx, emm0, etmp, y, mask, z;
        vminps(x, x, ymmword_ptr[constants + ConstantIndex::exp_hi * 32]);
        vmaxps(x, x, ymmword_ptr[constants + ConstantIndex::exp_lo * 32]);
        vmovaps(fx, ymmword_ptr[constants + ConstantIndex::log2e * 32]);
        vfmadd213ps(fx, x, ymmword_ptr[constants + ConstantIndex::float_half * 32]);
        vcvttps2dq(emm0, fx);
        vcvtdq2ps(etmp, emm0);
        vcmpps(mask, etmp, fx, _CMP_NLE_US);
        vandps(mask, mask, one);
        vsubps(fx, etmp, mask);
        vfnmadd231ps(x, fx, ymmword_ptr[constants + ConstantIndex::exp_c1 * 32]);
        vfnmadd231ps(x, fx, ymmword_ptr[constants + ConstantIndex::exp_c2 * 32]);
        vmulps(z, x, x);
        vmovaps(y, ymmword_ptr[constants + ConstantIndex::exp_p0 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::exp_p1 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::exp_p2 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::exp_p3 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::exp_p4 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::exp_p5 * 32]);
        vfmadd213ps(y, z, x);
        vaddps(y, y, one);
        vcvttps2dq(emm0, fx);
        vpaddd(emm0, emm0, ymmword_ptr[constants + ConstantIndex::x7F * 32]);
        vpslld(emm0, emm0, 23);
        vmulps(x, y, emm0);
    }

    void log_(YmmReg x, YmmReg zero, YmmReg one, Reg constants)
    {
        YmmReg emm0, invalid_mask, mask, y, etmp, z;
        vmaxps(x, x, ymmword_ptr[constants + ConstantIndex::min_norm_pos * 32]);
        vpsrld(emm0, x, 23);
        vandps(x, x, ymmword_ptr[constants + ConstantIndex::inv_mant_mask * 32]);
        vorps(x, x, ymmword_ptr[constants + ConstantIndex::float_half * 32]);
        vpsubd(emm0, emm0, ymmword_ptr[constants + ConstantIndex::x7F * 32]);
        vcvtdq2ps(emm0, emm0);
        vaddps(emm0, emm0, one);
        vcmpps(mask, x, ymmword_ptr[constants + ConstantIndex::sqrt_1_2 * 32], _CMP_LT_OS);
        vandps(etmp, x, mask);
        vsubps(x, x, one);
        vandps(mask, mask, one);
        vsubps(emm0, emm0, mask);
        vaddps(x, x, etmp);
        vmulps(z, x, x);
        vmovaps(y, ymmword_ptr[constants + ConstantIndex::log_p0 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p1 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p2 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p3 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p4 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p5 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p6 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p7 * 32]);
        vfmadd213ps(y, x, ymmword_ptr[constants + ConstantIndex::log_p8 * 32]);
        vmulps(y, y, x);
        vmulps(y, y, z);
        vfmadd231ps(y, emm0, ymmword_ptr[constants + ConstantIndex::log_q1 * 32]);
        vfnmadd231ps(y, z, ymmword_ptr[constants + ConstantIndex::float_half * 32]);
        vaddps(x, x, y);
        vfmadd231ps(x, emm0, ymmword_ptr[constants + ConstantIndex::log_q2 * 32]);
    }

    void exp(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            YmmReg one;
            vmovaps(one, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
            vmovaps(t1, t2);
            exp_(t1, one, constants);
        });
    }

    void log(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.dst];
            YmmReg one;
            vmovaps(one, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
            vmovaps(t1, t2);
            log_(t1, zero, one, constants);
        });
    }

    void pow(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            auto t1 = bytecodeRegs[insn.src1];
            auto t2 = bytecodeRegs[insn.src2];
            auto t3 = bytecodeRegs[insn.dst];

            YmmReg r1, one;
            vmovaps(one, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
            vmovaps(r1, t1);
            log_(r1, zero, one, constants);
            vmulps(r1, r1, t2);
            exp_(r1, one, constants);
            vmovaps(t3, r1);
        });
    }

    void sincos_(bool issin, const ExprInstruction &insn, Reg constants, std::unordered_map<int, YmmReg> &bytecodeRegs)
    {
        auto x = bytecodeRegs[insn.src1];
        auto y = bytecodeRegs[insn.dst];
        YmmReg t1, sign, t2, t3, t4;
        // Remove sign
        vmovaps(t1, ymmword_ptr[constants + ConstantIndex::absmask * 32]);
        if (issin) {
            vmovaps(sign, t1);
            vandnps(sign, sign, x);
        } else {
            vxorps(sign, sign, sign);
        }
        vandps(t1, t1, x);
        // Range reduction
        vmovaps(t3, ymmword_ptr[constants + ConstantIndex::float_rintf * 32]);
        vmulps(t2, t1, ymmword_ptr[constants + ConstantIndex::float_invpi * 32]);
        vaddps(t2, t2, t3);
        vpslld(t4, t2, 31);
        vxorps(sign, sign, t4);
        vsubps(t2, t2, t3);
        vfnmadd231ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_pi1 * 32]);
        vfnmadd231ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_pi2 * 32]);
        vfnmadd231ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_pi3 * 32]);
        vfnmadd231ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_pi4 * 32]);
        if (issin) {
            // Evaluate minimax polynomial for sin(x) in [-pi/2, pi/2] interval
            // Y <- X + X * X^2 * (C3 + X^2 * (C5 + X^2 * (C7 + X^2 * C9)))
            vmulps(t2, t1, t1);
            vmovaps(t3, ymmword_ptr[constants + ConstantIndex::float_sinC7 * 32]);
            vfmadd231ps(t3, t2, ymmword_ptr[constants + ConstantIndex::float_sinC9 * 32]);
            vfmadd213ps(t3, t2, ymmword_ptr[constants + ConstantIndex::float_sinC5 * 32]);
            vfmadd213ps(t3, t2, ymmword_ptr[constants + ConstantIndex::float_sinC3 * 32]);
            vmulps(t3, t3, t2);
            vfmadd231ps(t1, t1, t3);
        } else {
            // Evaluate minimax polynomial for cos(x) in [-pi/2, pi/2] interval
            // Y <- 1 + X^2 * (C2 + X^2 * (C4 + X^2 * (C6 + X^2 * C8)))
            vmulps(t2, t1, t1);
            vmovaps(t1, ymmword_ptr[constants + ConstantIndex::float_cosC6 * 32]);
            vfmadd231ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_cosC8 * 32]);
            vfmadd213ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_cosC4 * 32]);
            vfmadd213ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_cosC2 * 32]);
            vfmadd213ps(t1, t2, ymmword_ptr[constants + ConstantIndex::float_one * 32]);
        }
        // Apply sign
        vxorps(y, t1, sign);
    }

    void sin(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            sincos_(true, insn, constants, bytecodeRegs);
        });
    }

    void cos(const ExprInstruction &insn) override
    {
        deferred.push_back(EMIT()
        {
            sincos_(false, insn, constants, bytecodeRegs);
        });
    }

    void main(Reg regptrs, Reg regoffs, Reg niter)
    {
        std::unordered_map<int, YmmReg> bytecodeRegs;
        YmmReg zero;
        vpxor(zero, zero, zero);
        Reg constants;
        mov(constants, (uintptr_t)constData);

        L("wloop");

        for (const auto &f : deferred) {
            f(regptrs, zero, constants, bytecodeRegs);
        }

#if UINTPTR_MAX > UINT32_MAX
        for (int i = 0; i < numInputs / 4 + 1; i++) {
            YmmReg r1, r2;
            vmovdqu(r1, ymmword_ptr[regptrs + 32 * i]);
            vmovdqu(r2, ymmword_ptr[regoffs + 32 * i]);
            vpaddq(r1, r1, r2);
            vmovdqu(ymmword_ptr[regptrs + 32 * i], r1);
        }
#else
        for (int i = 0; i < numInputs / 8 + 1; i++) {
            YmmReg r1, r2;
            vmovdqu(r1, ymmword_ptr[regptrs + 32 * i]);
            vmovdqu(r2, ymmword_ptr[regoffs + 32 * i]);
            vpaddd(r1, r1, r2);
            vmovdqu(ymmword_ptr[regptrs + 32 * i], r1);
        }
#endif

        jit::sub(niter, 1);
        jnz("wloop");
    }

public:
    explicit ExprCompiler256(int numInputs) : cpuFeatures(*getCPUFeatures()), numInputs(numInputs) {}

    std::pair<ExprData::ProcessLineProc, size_t> getCode() override
    {
        size_t size;
        if (jit::GetCode(true) && (size = GetCodeSize())) {
#ifdef VS_TARGET_OS_WINDOWS
            void *ptr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
            void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#endif
            memcpy(ptr, jit::GetCode(true), size);
            return {reinterpret_cast<ExprData::ProcessLineProc>(ptr), size};
        }
        return {nullptr, 0};
    }
#undef EMIT
};

constexpr ExprUnion ExprCompiler256::constData alignas(32)[53][8];

std::unique_ptr<ExprCompiler> make_compiler(int numInputs, int cpulevel)
{
    if (getCPUFeatures()->avx2 && cpulevel >= VS_CPU_LEVEL_AVX2)
        return std::unique_ptr<ExprCompiler>(new ExprCompiler256(numInputs));
    else
        return std::unique_ptr<ExprCompiler>(new ExprCompiler128(numInputs));
}
#endif

class ExprInterpreter {
    const ExprInstruction *bytecode;
    size_t numInsns;
    std::vector<float> registers;

    template <class T>
    static T clamp_int(float x, int depth = std::numeric_limits<T>::digits)
    {
        float maxval = static_cast<float>((1U << depth) - 1);
        return static_cast<T>(std::lrint(std::min(std::max(x, static_cast<float>(std::numeric_limits<T>::min())), maxval)));
    }

    static float bool2float(bool x) { return x ? 1.0f : 0.0f; }
    static bool float2bool(float x) { return x > 0.0f; }
public:
    ExprInterpreter(const ExprInstruction *bytecode, size_t numInsns) : bytecode(bytecode), numInsns(numInsns)
    {
        int maxreg = 0;
        for (size_t i = 0; i < numInsns; ++i) {
            maxreg = std::max(maxreg, bytecode[i].dst);
        }
        registers.resize(maxreg + 1);
    }

    void eval(const uint8_t * const *srcp, uint8_t *dstp, int x)
    {
        for (size_t i = 0; i < numInsns; ++i) {
            const ExprInstruction &insn = bytecode[i];

#define SRC1 registers[insn.src1]
#define SRC2 registers[insn.src2]
#define SRC3 registers[insn.src3]
#define DST registers[insn.dst]
            switch (insn.op.type) {
            case ExprOpType::MEM_LOAD_U8: DST = reinterpret_cast<const uint8_t *>(srcp[insn.op.imm.u])[x]; break;
            case ExprOpType::MEM_LOAD_U16: DST = reinterpret_cast<const uint16_t *>(srcp[insn.op.imm.u])[x]; break;
            case ExprOpType::MEM_LOAD_F16: DST = 0; break;
            case ExprOpType::MEM_LOAD_F32: DST = reinterpret_cast<const float *>(srcp[insn.op.imm.u])[x]; break;
            case ExprOpType::CONSTANT: DST = insn.op.imm.f; break;
            case ExprOpType::ADD: DST = SRC1 + SRC2; break;
            case ExprOpType::SUB: DST = SRC1 - SRC2; break;
            case ExprOpType::MUL: DST = SRC1 * SRC2; break;
            case ExprOpType::DIV: DST = SRC1 / SRC2; break;
            case ExprOpType::FMA:
                switch (static_cast<FMAType>(insn.op.imm.u)) {
                case FMAType::FMADD: DST = SRC2 * SRC3 + SRC1; break;
                case FMAType::FMSUB: DST = SRC2 * SRC3 - SRC1; break;
                case FMAType::FNMADD: DST = -(SRC2 * SRC3) + SRC1; break;
                case FMAType::FNMSUB: DST = -(SRC2 * SRC3) - SRC1; break;
                };
                break;
            case ExprOpType::MAX: DST = std::max(SRC1, SRC2); break;
            case ExprOpType::MIN: DST = std::min(SRC1, SRC2); break;
            case ExprOpType::EXP: DST = std::exp(SRC1); break;
            case ExprOpType::LOG: DST = std::log(SRC1); break;
            case ExprOpType::POW: DST = std::pow(SRC1, SRC2); break;
            case ExprOpType::SQRT: DST = std::sqrt(SRC1); break;
            case ExprOpType::SIN: DST = std::sin(SRC1); break;
            case ExprOpType::COS: DST = std::cos(SRC1); break;
            case ExprOpType::ABS: DST = std::fabs(SRC1); break;
            case ExprOpType::NEG: DST = -SRC1; break;
            case ExprOpType::CMP:
                switch (static_cast<ComparisonType>(insn.op.imm.u)) {
                case ComparisonType::EQ: DST = bool2float(SRC1 == SRC2); break;
                case ComparisonType::LT: DST = bool2float(SRC1 < SRC2); break;
                case ComparisonType::LE: DST = bool2float(SRC1 <= SRC2); break;
                case ComparisonType::NEQ: DST = bool2float(SRC1 != SRC2); break;
                case ComparisonType::NLT: DST = bool2float(SRC1 >= SRC2); break;
                case ComparisonType::NLE: DST = bool2float(SRC1 > SRC2); break;
                }
                break;
            case ExprOpType::TERNARY: DST = float2bool(SRC1) ? SRC2 : SRC3; break;
            case ExprOpType::AND: DST = bool2float((float2bool(SRC1) && float2bool(SRC2))); break;
            case ExprOpType::OR:  DST = bool2float((float2bool(SRC1) || float2bool(SRC2))); break;
            case ExprOpType::XOR: DST = bool2float((float2bool(SRC1) != float2bool(SRC2))); break;
            case ExprOpType::NOT: DST = bool2float(!float2bool(SRC1)); break;
            case ExprOpType::MEM_STORE_U8:  reinterpret_cast<uint8_t *>(dstp)[x] = clamp_int<uint8_t>(SRC1); return;
            case ExprOpType::MEM_STORE_U16: reinterpret_cast<uint16_t *>(dstp)[x] = clamp_int<uint16_t>(SRC1, insn.op.imm.u); return;
            case ExprOpType::MEM_STORE_F16: reinterpret_cast<uint16_t *>(dstp)[x] = 0; return;
            case ExprOpType::MEM_STORE_F32: reinterpret_cast<float *>(dstp)[x] = SRC1; return;
            default: fprintf(stderr, "%s", "illegal opcode\n"); std::terminate(); return;
            }
#undef DST
#undef SRC3
#undef SRC2
#undef SRC1
        }
    }
};

static const VSFrame *VS_CC exprGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ExprData *d = static_cast<ExprData *>(instanceData);
    int numInputs = d->numInputs;

    if (activationReason == arInitial) {
        for (int i = 0; i < numInputs; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < numInputs; i++)
            src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);

        int height = vsapi->getFrameHeight(src[0], 0);
        int width = vsapi->getFrameWidth(src[0], 0);
        int planes[3] = { 0, 1, 2 };
        const VSFrame *srcf[3] = { d->plane[0] != poCopy ? nullptr : src[0], d->plane[1] != poCopy ? nullptr : src[0], d->plane[2] != poCopy ? nullptr : src[0] };
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi.format, width, height, srcf, planes, src[0], core);

        const uint8_t *srcp[MAX_EXPR_INPUTS] = {};
        ptrdiff_t src_stride[MAX_EXPR_INPUTS] = {};
        alignas(32) intptr_t ptroffsets[((MAX_EXPR_INPUTS + 1) + 7) & ~7] = { d->vi.format.bytesPerSample * 8 };

        for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
            if (d->plane[plane] != poProcess)
                continue;

            for (int i = 0; i < numInputs; i++) {
                if (d->node[i]) {
                    srcp[i] = vsapi->getReadPtr(src[i], plane);
                    src_stride[i] = vsapi->getStride(src[i], plane);
                    ptroffsets[i + 1] = vsapi->getVideoFrameFormat(src[i])->bytesPerSample * 8;
                }
            }

            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(dst, plane);
            int w = vsapi->getFrameWidth(dst, plane);

            if (d->proc[plane]) {
                ExprData::ProcessLineProc proc = d->proc[plane];
                int niterations = (w + 7) / 8;

                for (int i = 0; i < numInputs; i++) {
                    if (d->node[i])
                        ptroffsets[i + 1] = vsapi->getVideoFrameFormat(src[i])->bytesPerSample * 8;
                }

                for (int y = 0; y < h; y++) {
                    alignas(32) uint8_t *rwptrs[((MAX_EXPR_INPUTS + 1) + 7) & ~7] = { dstp + dst_stride * y };
                    for (int i = 0; i < numInputs; i++) {
                        rwptrs[i + 1] = const_cast<uint8_t *>(srcp[i] + src_stride[i] * y);
                    }
                    proc(rwptrs, ptroffsets, niterations);
                }
            } else {
                ExprInterpreter interpreter(d->bytecode[plane].data(), d->bytecode[plane].size());

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
    const CPUFeatures &f = *getCPUFeatures();
#   define EXPR_F16C_TEST (f.f16c)
#else
#   define EXPR_F16C_TEST (false)
#endif

    try {
        d->numInputs = vsapi->mapNumElements(in, "clips");
        if (d->numInputs > 26)
            throw std::runtime_error("More than 26 input clips provided");

        for (int i = 0; i < d->numInputs; i++) {
            d->node[i] = vsapi->mapGetNode(in, "clips", i, &err);
        }

        const VSVideoInfo *vi[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < d->numInputs; i++) {
            if (d->node[i])
                vi[i] = vsapi->getVideoInfo(d->node[i]);
        }

        for (int i = 0; i < d->numInputs; i++) {
            if (!isConstantVideoFormat(vi[i]))
                throw std::runtime_error("Only clips with constant format and dimensions allowed");
            if (vi[0]->format.numPlanes != vi[i]->format.numPlanes
                || vi[0]->format.subSamplingW != vi[i]->format.subSamplingW
                || vi[0]->format.subSamplingH != vi[i]->format.subSamplingH
                || vi[0]->width != vi[i]->width
                || vi[0]->height != vi[i]->height)
            {
                throw std::runtime_error("All inputs must have the same number of planes and the same dimensions, subsampling included");
            }

            if (EXPR_F16C_TEST) {
                if ((vi[i]->format.bitsPerSample > 16 && vi[i]->format.sampleType == stInteger)
                    || (vi[i]->format.bitsPerSample != 16 && vi[i]->format.bitsPerSample != 32 && vi[i]->format.sampleType == stFloat))
                    throw std::runtime_error("Input clips must be 8-16 bit integer or 16/32 bit float format");
            } else {
                if ((vi[i]->format.bitsPerSample > 16 && vi[i]->format.sampleType == stInteger)
                    || (vi[i]->format.bitsPerSample != 32 && vi[i]->format.sampleType == stFloat))
                    throw std::runtime_error("Input clips must be 8-16 bit integer or 32 bit float format");
            }
        }

        d->vi = *vi[0];
        int format = vsapi->mapGetIntSaturated(in, "format", 0, &err);
        if (!err) {
            VSVideoFormat f;
            if (vsapi->getVideoFormatByID(&f, format, core) && f.colorFamily != cfUndefined) {
                if (d->vi.format.numPlanes != f.numPlanes)
                    throw std::runtime_error("The number of planes in the inputs and output must match");
                vsapi->queryVideoFormat(&d->vi.format, d->vi.format.colorFamily, f.sampleType, f.bitsPerSample, d->vi.format.subSamplingW, d->vi.format.subSamplingH, core);
            }
        }

        int nexpr = vsapi->mapNumElements(in, "expr");
        if (nexpr > d->vi.format.numPlanes)
            throw std::runtime_error("More expressions given than there are planes");

        std::string expr[3];
        for (int i = 0; i < nexpr; i++) {
            expr[i] = vsapi->mapGetData(in, "expr", i, nullptr);
        }
        for (int i = nexpr; i < 3; ++i) {
            expr[i] = expr[nexpr - 1];
        }

        for (int i = 0; i < d->vi.format.numPlanes; i++) {
            if (!expr[i].empty()) {
                d->plane[i] = poProcess;
            } else {
                if (d->vi.format.bitsPerSample == vi[0]->format.bitsPerSample && d->vi.format.sampleType == vi[0]->format.sampleType)
                    d->plane[i] = poCopy;
                else
                    d->plane[i] = poUndefined;
            }

            if (d->plane[i] != poProcess)
                continue;

            d->bytecode[i] = compile(expr[i], vi, d->numInputs, d->vi);

            int cpulevel = vs_get_cpulevel(core);
            if (cpulevel > VS_CPU_LEVEL_NONE) {
#ifdef VS_TARGET_CPU_X86
                std::unique_ptr<ExprCompiler> compiler = make_compiler(d->numInputs, cpulevel);
                for (auto op : d->bytecode[i]) {
                    compiler->addInstruction(op, core, vsapi);
                }

                std::tie(d->proc[i], d->procSize[i]) = compiler->getCode();
#endif
            }
        }
#ifdef VS_TARGET_OS_WINDOWS
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
#endif
    } catch (std::runtime_error &e) {
        for (int i = 0; i < MAX_EXPR_INPUTS; i++) {
            vsapi->freeNode(d->node[i]);
        }
        vsapi->mapSetError(out, (std::string{ "Expr: " } + e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < d->numInputs; i++)
        deps.push_back({d->node[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->node[i])->numFrames) ? rpStrictSpatial : rpGeneral});
    vsapi->createVideoFilter(out, "Expr", &d->vi, exprGetFrame, exprFree, fmParallel, deps.data(), d->numInputs, d.get(), core);
    d.release();
}

} // namespace


//////////////////////////////////////////
// Init

void exprInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Expr", "clips:vnode[];expr:data[];format:int:opt;", "clip:vnode;", exprCreate, nullptr, plugin);
}
