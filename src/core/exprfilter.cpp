/*
* Copyright (c) 2012-2015 Fredrik Mellbin
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

#include <iostream>
#include <locale>
#include <sstream>
#include <vector>
#include <stack>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <cmath>
#include "VapourSynth.h"
#include "VSHelper.h"
#include "exprfilter.h"
#include "cpufeatures.h"
#ifdef VS_TARGET_CPU_X86
#define NOMINMAX
#include "jitasm.h"
#ifndef VS_TARGET_OS_WINDOWS
#include <sys/mman.h>
#endif
#endif


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

typedef enum {
    opLoadSrc8, opLoadSrc16, opLoadSrcF32, opLoadSrcF16, opLoadConst,
    opStore8, opStore16, opStoreF32, opStoreF16,
    opDup, opSwap,
    opAdd, opSub, opMul, opDiv, opMax, opMin, opSqrt, opAbs,
    opGt, opLt, opEq, opLE, opGE, opTernary,
    opAnd, opOr, opXor, opNeg,
    opExp, opLog, opPow
} SOperation;

typedef union {
    float fval;
    int32_t ival;
} ExprUnion;

struct FloatIntUnion {
    ExprUnion u;
    FloatIntUnion(int32_t i) { u.ival = i; }
    FloatIntUnion(float f) { u.fval = f; }
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
#ifdef VS_TARGET_CPU_X86
    typedef void(*ProcessLineProc)(void *rwptrs, intptr_t ptroff[MAX_EXPR_INPUTS + 1], intptr_t niter);
    ProcessLineProc proc[3];
    ExprData() : node(), vi(), proc() {}
#else
    ExprData() : node(), vi() {}
#endif
    ~ExprData() {
#ifdef VS_TARGET_CPU_X86
        for (int i = 0; i < 3; i++)
#ifdef VS_TARGET_OS_WINDOWS
            VirtualFree((LPVOID)proc[i], 0, MEM_RELEASE);
#else
            munmap((void *)proc[i], 0);
#endif
#endif
    }
};

#ifdef VS_TARGET_CPU_X86

#define OneArgOp(instr) \
auto &t1 = stack.top(); \
instr(t1.first, t1.first); \
instr(t1.second, t1.second);

#define TwoArgOp(instr) \
auto t1 = stack.top(); \
stack.pop(); \
auto &t2 = stack.top(); \
instr(t2.first, t1.first); \
instr(t2.second, t1.second);

#define CmpOp(instr) \
auto t1 = stack.top(); \
stack.pop(); \
auto t2 = stack.top(); \
stack.pop(); \
instr(t1.first, t2.first); \
instr(t1.second, t2.second); \
andps(t1.first, CPTR(elfloat_one)); \
andps(t1.second, CPTR(elfloat_one)); \
stack.push(t1);

#define LogicOp(instr) \
auto t1 = stack.top(); \
stack.pop(); \
auto t2 = stack.top(); \
stack.pop(); \
cmpnleps(t1.first, zero); \
cmpnleps(t1.second, zero); \
cmpnleps(t2.first, zero); \
cmpnleps(t2.second, zero); \
instr(t1.first, t2.first); \
instr(t1.second, t2.second); \
andps(t1.first, CPTR(elfloat_one)); \
andps(t1.second, CPTR(elfloat_one)); \
stack.push(t1);

enum {
    elabsmask, elc7F, elmin_norm_pos, elinv_mant_mask,
    elfloat_one, elfloat_half, elstore8, elstore16,
    elexp_hi, elexp_lo, elcephes_LOG2EF, elcephes_exp_C1, elcephes_exp_C2, elcephes_exp_p0, elcephes_exp_p1, elcephes_exp_p2, elcephes_exp_p3, elcephes_exp_p4, elcephes_exp_p5, elcephes_SQRTHF,
    elcephes_log_p0, elcephes_log_p1, elcephes_log_p2, elcephes_log_p3, elcephes_log_p4, elcephes_log_p5, elcephes_log_p6, elcephes_log_p7, elcephes_log_p8, elcephes_log_q1 = elcephes_exp_C2, elcephes_log_q2 = elcephes_exp_C1
};

#define XCONST(x) { x, x, x, x }

alignas(16) static const FloatIntUnion logexpconst[][4] = {
    XCONST(0x7FFFFFFF), // absmask
    XCONST(0x7F), // c7F
    XCONST(0x00800000), // min_norm_pos
    XCONST(~0x7f800000), // inv_mant_mask
    XCONST(1.0f), // float_one
    XCONST(0.5f), // float_half
    XCONST(255.0f), // store8
    XCONST(65535.0f), // store16
    XCONST(88.3762626647949f), // exp_hi
    XCONST(-88.3762626647949f), // exp_lo
    XCONST(1.44269504088896341f), // cephes_LOG2EF
    XCONST(0.693359375f), // cephes_exp_C1
    XCONST(-2.12194440e-4f), // cephes_exp_C2
    XCONST(1.9875691500E-4f), // cephes_exp_p0
    XCONST(1.3981999507E-3f), // cephes_exp_p1
    XCONST(8.3334519073E-3f), // cephes_exp_p2
    XCONST(4.1665795894E-2f), // cephes_exp_p3
    XCONST(1.6666665459E-1f), // cephes_exp_p4
    XCONST(5.0000001201E-1f), // cephes_exp_p5
    XCONST(0.707106781186547524f), // cephes_SQRTHF
    XCONST(7.0376836292E-2f), // cephes_log_p0
    XCONST(-1.1514610310E-1f), // cephes_log_p1
    XCONST(1.1676998740E-1f), // cephes_log_p2
    XCONST(-1.2420140846E-1f), // cephes_log_p3
    XCONST(+1.4249322787E-1f), // cephes_log_p4
    XCONST(-1.6668057665E-1f), // cephes_log_p5
    XCONST(+2.0000714765E-1f), // cephes_log_p6
    XCONST(-2.4999993993E-1f), // cephes_log_p7
    XCONST(+3.3333331174E-1f) // cephes_log_p8
};


#define CPTR(x) (xmmword_ptr[constptr + (x) * 16])

#define EXP_PS(x) { \
XmmReg fx, emm0, etmp, y, mask, z; \
minps(x, CPTR(elexp_hi)); \
maxps(x, CPTR(elexp_lo)); \
movaps(fx, x); \
mulps(fx, CPTR(elcephes_LOG2EF)); \
addps(fx, CPTR(elfloat_half)); \
cvttps2dq(emm0, fx); \
cvtdq2ps(etmp, emm0); \
movaps(mask, etmp); \
cmpnleps(mask, fx); \
andps(mask, CPTR(elfloat_one)); \
movaps(fx, etmp); \
subps(fx, mask); \
movaps(etmp, fx); \
mulps(etmp, CPTR(elcephes_exp_C1)); \
movaps(z, fx); \
mulps(z, CPTR(elcephes_exp_C2)); \
subps(x, etmp); \
subps(x, z); \
movaps(z, x); \
mulps(z, z); \
movaps(y, CPTR(elcephes_exp_p0)); \
mulps(y, x); \
addps(y, CPTR(elcephes_exp_p1)); \
mulps(y, x); \
addps(y, CPTR(elcephes_exp_p2)); \
mulps(y, x); \
addps(y, CPTR(elcephes_exp_p3)); \
mulps(y, x); \
addps(y, CPTR(elcephes_exp_p4)); \
mulps(y, x); \
addps(y, CPTR(elcephes_exp_p5)); \
mulps(y, z); \
addps(y, x); \
addps(y, CPTR(elfloat_one)); \
cvttps2dq(emm0, fx); \
paddd(emm0, CPTR(elc7F)); \
pslld(emm0, 23); \
mulps(y, emm0); \
x = y; }

#define LOG_PS(x) { \
XmmReg emm0, invalid_mask, mask, y, etmp, z; \
xorps(invalid_mask, invalid_mask); \
cmpnleps(invalid_mask, x); \
maxps(x, CPTR(elmin_norm_pos)); \
movaps(emm0, x); \
psrld(emm0, 23); \
andps(x, CPTR(elinv_mant_mask)); \
orps(x, CPTR(elfloat_half)); \
psubd(emm0, CPTR(elc7F)); \
cvtdq2ps(emm0, emm0); \
addps(emm0, CPTR(elfloat_one)); \
movaps(mask, x); \
cmpltps(mask, CPTR(elcephes_SQRTHF)); \
movaps(etmp, x); \
andps(etmp, mask); \
subps(x, CPTR(elfloat_one)); \
andps(mask, CPTR(elfloat_one)); \
subps(emm0, mask); \
addps(x, etmp); \
movaps(z, x); \
mulps(z, z); \
movaps(y, CPTR(elcephes_log_p0)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p1)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p2)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p3)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p4)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p5)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p6)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p7)); \
mulps(y, x); \
addps(y, CPTR(elcephes_log_p8)); \
mulps(y, x); \
mulps(y, z); \
movaps(etmp, emm0); \
mulps(etmp, CPTR(elcephes_log_q1)); \
addps(y, etmp); \
mulps(z, CPTR(elfloat_half)); \
subps(y, z); \
mulps(emm0, CPTR(elcephes_log_q2)); \
addps(x, y); \
addps(x, emm0); \
orps(x, invalid_mask); }

struct ExprEval : public jitasm::function<void, ExprEval, uint8_t *, const intptr_t *, intptr_t> {

    std::vector<ExprOp> ops;
    int numInputs;

    ExprEval(std::vector<ExprOp> &ops, int numInputs) : ops(ops), numInputs(numInputs) {}

    void main(Reg regptrs, Reg regoffs, Reg niter)
    {
        XmmReg zero;
        pxor(zero, zero);
        Reg constptr;
        mov(constptr, (uintptr_t)logexpconst);

        L("wloop");

        std::stack<std::pair<XmmReg, XmmReg>> stack;
        for (const auto &iter : ops) {
            if (iter.op == opLoadSrc8) {
                XmmReg r1, r2;
                Reg a;
                mov(a, ptr[regptrs + sizeof(void *) * (iter.e.ival + 1)]);
                movq(r1, mmword_ptr[a]);
                punpcklbw(r1, zero);
                movdqa(r2, r1);
                punpckhwd(r1, zero);
                punpcklwd(r2, zero);
                cvtdq2ps(r1, r1);
                cvtdq2ps(r2, r2);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opLoadSrc16) {
                XmmReg r1, r2;
                Reg a;
                mov(a, ptr[regptrs + sizeof(void *) * (iter.e.ival + 1)]);
                movdqa(r1, xmmword_ptr[a]);
                movdqa(r2, r1);
                punpckhwd(r1, zero);
                punpcklwd(r2, zero);
                cvtdq2ps(r1, r1);
                cvtdq2ps(r2, r2);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opLoadSrcF32) {
                XmmReg r1, r2;
                Reg a;
                mov(a, ptr[regptrs + sizeof(void *) * (iter.e.ival + 1)]);
                movdqa(r1, xmmword_ptr[a]);
                movdqa(r2, xmmword_ptr[a + 16]);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opLoadSrcF16) {
                XmmReg r1, r2;
                Reg a;
                mov(a, ptr[regptrs + sizeof(void *) * (iter.e.ival + 1)]);
                vcvtph2ps(r1, qword_ptr[a]);
                vcvtph2ps(r2, qword_ptr[a + 8]);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opLoadConst) {
                XmmReg r1, r2;
                Reg a;
                mov(a, iter.e.ival);
                movd(r1, a);
                shufps(r1, r1, 0);
                movaps(r2, r1);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opDup) {
                XmmReg r1, r2;
                movaps(r1, stack.top().first);
                movaps(r2, stack.top().second);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opSwap) {
                auto t1 = stack.top();
                stack.pop();
                auto t2 = stack.top();
                stack.pop();
                stack.push(t1);
                stack.push(t2);
            } else if (iter.op == opAdd) {
                TwoArgOp(addps)
            } else if (iter.op == opSub) {
                TwoArgOp(subps)
            } else if (iter.op == opMul) {
                TwoArgOp(mulps)
            } else if (iter.op == opDiv) {
                TwoArgOp(divps)
            } else if (iter.op == opMax) {
                TwoArgOp(maxps)
            } else if (iter.op == opMin) {
                TwoArgOp(minps)
            } else if (iter.op == opSqrt) {
                auto &t1 = stack.top();
                maxps(t1.first, zero);
                maxps(t1.second, zero);
                sqrtps(t1.first, t1.first);
                sqrtps(t1.second, t1.second);
            } else if (iter.op == opStore8) {
                auto t1 = stack.top();
                stack.pop();
                XmmReg r1, r2;
                Reg a;
                maxps(t1.first, zero);
                maxps(t1.second, zero);  
                minps(t1.first, CPTR(elstore8));
                minps(t1.second, CPTR(elstore8));
                mov(a, ptr[regptrs]);
                cvtps2dq(t1.first, t1.first);
                cvtps2dq(t1.second, t1.second);
                movdqa(r1, t1.first);
                movdqa(r2, t1.second);
                psrldq(t1.first, 6);
                psrldq(t1.second, 6);
                por(t1.first, r1);
                por(t1.second, r2);
                pshuflw(t1.first, t1.first, 0b11011000);
                pshuflw(t1.second, t1.second, 0b11011000);
                punpcklqdq(t1.second, t1.first);
                packuswb(t1.second, zero);
                movq(mmword_ptr[a], t1.second);
            } else if (iter.op == opStore16) {
                auto t1 = stack.top();
                stack.pop();
                XmmReg r1, r2;
                Reg a;
                maxps(t1.first, zero);
                maxps(t1.second, zero);
                minps(t1.first, CPTR(elstore16));
                minps(t1.second, CPTR(elstore16));
                mov(a, ptr[regptrs]);
                cvtps2dq(t1.first, t1.first);
                cvtps2dq(t1.second, t1.second);
                movdqa(r1, t1.first);
                movdqa(r2, t1.second);
                psrldq(t1.first, 6);
                psrldq(t1.second, 6);
                por(t1.first, r1);
                por(t1.second, r2);
                pshuflw(t1.first, t1.first, 0b11011000);
                pshuflw(t1.second, t1.second, 0b11011000);
                punpcklqdq(t1.second, t1.first);
                movdqa(xmmword_ptr[a], t1.second);
            } else if (iter.op == opStoreF32) {
                auto t1 = stack.top();
                stack.pop();
                Reg a;
                mov(a, ptr[regptrs]);
                movaps(xmmword_ptr[a], t1.first);
                movaps(xmmword_ptr[a + 16], t1.second);
            } else if (iter.op == opStoreF16) {
                auto t1 = stack.top();
                stack.pop();
                Reg a;
                mov(a, ptr[regptrs]);
                vcvtps2ph(qword_ptr[a], t1.first, 0);
                vcvtps2ph(qword_ptr[a + 8], t1.second, 0);
            } else if (iter.op == opAbs) {
                auto &t1 = stack.top();
                andps(t1.first, CPTR(elabsmask));
                andps(t1.second, CPTR(elabsmask));
            } else if (iter.op == opNeg) {
                auto &t1 = stack.top();
                cmpleps(t1.first, zero);
                cmpleps(t1.second, zero);
                andps(t1.first, CPTR(elfloat_one));
                andps(t1.second, CPTR(elfloat_one));
            } else if (iter.op == opAnd) {
                LogicOp(andps)
            } else if (iter.op == opOr) {
                LogicOp(orps)
            } else if (iter.op == opXor) {
                LogicOp(xorps)
            } else if (iter.op == opGt) {
                CmpOp(cmpltps)
            } else if (iter.op == opLt) {
                CmpOp(cmpnleps)
            } else if (iter.op == opEq) {
                CmpOp(cmpeqps)
            } else if (iter.op == opLE) {
                CmpOp(cmpnltps)
            } else if (iter.op == opGE) {
                CmpOp(cmpleps)
            } else if (iter.op == opTernary) {
                auto t1 = stack.top();
                stack.pop();
                auto t2 = stack.top();
                stack.pop();
                auto t3 = stack.top();
                stack.pop();
                XmmReg r1, r2;
                xorps(r1, r1);
                xorps(r2, r2);
                cmpltps(r1, t3.first);
                cmpltps(r2, t3.second);
                andps(t2.first, r1);
                andps(t2.second, r2);
                andnps(r1, t1.first);
                andnps(r2, t1.second);
                orps(r1, t2.first);
                orps(r2, t2.second);
                stack.push(std::make_pair(r1, r2));
            } else if (iter.op == opExp) {
                auto &t1 = stack.top();
                EXP_PS(t1.first)
                EXP_PS(t1.second)
            } else if (iter.op == opLog) {
                auto &t1 = stack.top();
                LOG_PS(t1.first)
                LOG_PS(t1.second)
            } else if (iter.op == opPow) {
                auto t1 = stack.top();
                stack.pop();
                auto &t2 = stack.top();
                LOG_PS(t2.first)
                mulps(t2.first, t1.first);
                EXP_PS(t2.first)
                LOG_PS(t2.second)
                mulps(t2.second, t1.second);
                EXP_PS(t2.second)
            }
        }

        if (sizeof(void *) == 8) {
            int numIter = (numInputs + 1 + 1) / 2;

            for (int i = 0; i < numIter; i++) {
                XmmReg r1, r2;
                movdqu(r1, xmmword_ptr[regptrs + 16 * i]);
                movdqu(r2, xmmword_ptr[regoffs + 16 * i]);
                paddq(r1, r2);
                movdqu(xmmword_ptr[regptrs + 16 * i], r1);
            }
        } else {
            int numIter = (numInputs + 1 + 3) / 4;
            for (int i = 0; i < numIter; i++) {
                XmmReg r1, r2;
                movdqu(r1, xmmword_ptr[regptrs + 16 * i]);
                movdqu(r2, xmmword_ptr[regoffs + 16 * i]);
                paddd(r1, r2);
                movdqu(xmmword_ptr[regptrs + 16 * i], r1);
            }
        }

        sub(niter, 1);
        jnz("wloop");
    }
};
#endif

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

#ifdef VS_TARGET_CPU_X86
        intptr_t ptroffsets[MAX_EXPR_INPUTS + 1] = { d->vi.format->bytesPerSample * 8 };

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->plane[plane] == poProcess) {
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
                int niterations = (w + 7) / 8;

                ExprData::ProcessLineProc proc = d->proc[plane];

                for (int y = 0; y < h; y++) {
                    const uint8_t *rwptrs[MAX_EXPR_INPUTS + 1] = { dstp + dst_stride * y };
                    for (int i = 0; i < numInputs; i++)
                        rwptrs[i + 1] = srcp[i] + src_stride[i] * y;
                    proc(rwptrs, ptroffsets, niterations);
                }
            }
        }

#else
        std::vector<float> stackVector(d->maxStackSize);

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->plane[plane] == poProcess) {
                for (int i = 0; i < numInputs; i++) {
                    if (d->node[i]) {
                        srcp[i] = vsapi->getReadPtr(src[i], plane);
                        src_stride[i] = vsapi->getStride(src[i], plane);
                    } 
                }

                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src[0], plane);
                int w = vsapi->getFrameWidth(src[0], plane);
                const ExprOp *vops = d->ops[plane].data();
                float *stack = stackVector.data();
                float stacktop = 0;
                float tmp;

                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
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
                                ++si;
                                break;
                            case opSwap:
                                tmp = stacktop;
                                stacktop = stack[si];
                                stack[si] = tmp;
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
                                dstp[x] = std::max(0.0f, std::min(stacktop, 255.0f)) + 0.5f;
                                goto loopend;
                            case opStore16:
                                reinterpret_cast<uint16_t *>(dstp)[x] = std::max(0.0f, std::min(stacktop, 65535.0f)) + 0.5f;
                                goto loopend;
                            case opStoreF32:
                                reinterpret_cast<float *>(dstp)[x] = stacktop;
                                goto loopend;
                            }
                        }
                        loopend:;
                    }
                    dstp += dst_stride;
                    for (int i = 0; i < numInputs; i++)
                        srcp[i] += src_stride[i];
                }
            }
        }
#endif
        for (int i = 0; i < MAX_EXPR_INPUTS; i++)
            vsapi->freeFrame(src[i]);
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

#define LOAD_OP(op,v) do { ops.push_back(ExprOp(op, (v))); maxStackSize = std::max(++stackSize, maxStackSize); } while(0)
#define GENERAL_OP(op, req, dec) do { if (stackSize < req) throw std::runtime_error("Not enough elements on stack to perform operation " + tokens[i]); ops.push_back(ExprOp(op)); stackSize-=(dec); } while(0)
#define ONE_ARG_OP(op) GENERAL_OP(op, 1, 0)
#define TWO_ARG_OP(op) GENERAL_OP(op, 2, 1)
#define THREE_ARG_OP(op) GENERAL_OP(op, 3, 2)

static size_t parseExpression(const std::string &expr, std::vector<ExprOp> &ops, const VSVideoInfo **vi, const SOperation storeOp, int numInputs) {
    std::vector<std::string> tokens;
    split(tokens, expr, " ", split1::no_empties);

    size_t maxStackSize = 0;
    size_t stackSize = 0;

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
        else if (tokens[i] == "dup")
            LOAD_OP(opDup, 0);
        else if (tokens[i] == "swap")
            GENERAL_OP(opSwap, 2, 0);
        else if (tokens[i].length() == 1 && tokens[i][0] >= 'a' && tokens[i][0] <= 'z') {
            char srcChar = tokens[i][0];
            int loadIndex;
            if (srcChar >= 'x')
                loadIndex = srcChar - 'x';
            else
                loadIndex = srcChar - 'a' + 3;
            if (loadIndex >= numInputs)
                throw std::runtime_error("Too few input clips supplied to reference '" + tokens[i] + "'");
            LOAD_OP(getLoadOp(vi[loadIndex]), loadIndex);
        } else {
            float f;
            std::string s;
            std::istringstream numStream(tokens[i]);
            numStream.imbue(std::locale::classic());
            if (!(numStream >> f))
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float");
            if (numStream >> s)
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float, not the whole token could be converted");
            LOAD_OP(opLoadConst, f);
        }
    }

    if (tokens.size() > 0) {
        if (stackSize != 1)
            throw std::runtime_error("Stack unbalanced at end of expression. Need to have exactly one value on the stack to return.");
        ops.push_back(storeOp);
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
        case opDup:
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

    if (operands == 1) {
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

static void foldConstants(std::vector<ExprOp> &ops) {
    for (size_t i = 0; i < ops.size(); i++) {
        switch (ops[i].op) {
            case opDup:
                if (ops[i - 1].op == opLoadConst) {
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
                if (ops[i - 2].op == opLoadConst && ops[i - 1].op == opLoadConst) {
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
                if (ops[start1].op == opLoadConst) {
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
            if ((vi[i]->format->bitsPerSample > 16 && vi[i]->format->sampleType == stInteger)
                || (vi[i]->format->bitsPerSample != 32 && vi[i]->format->sampleType == stFloat))
                throw std::runtime_error("Input clips must be 8-16 bit integer or 32 bit float format");
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
            d->maxStackSize = std::max(parseExpression(expr[i], d->ops[i], vi, getStoreOp(&d->vi), d->numInputs), d->maxStackSize);
            foldConstants(d->ops[i]);
        }

#ifdef VS_TARGET_CPU_X86
        for (int i = 0; i < d->vi.format->numPlanes; i++) {
            if (d->plane[i] == poProcess) {
                ExprEval ExprObj(d->ops[i], d->numInputs);
                if (ExprObj.GetCode() && ExprObj.GetCodeSize()) {
#ifdef VS_TARGET_OS_WINDOWS
                    d->proc[i] = (ExprData::ProcessLineProc)VirtualAlloc(nullptr, ExprObj.GetCodeSize(), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
                    d->proc[i] = (ExprData::ProcessLineProc)mmap(nullptr, ExprObj.GetCodeSize(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#endif
                    memcpy((void *)d->proc[i], ExprObj.GetCode(), ExprObj.GetCodeSize());
                }
            }
        }
#ifdef VS_TARGET_OS_WINDOWS
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
#endif
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

//////////////////////////////////////////
// Init

void VS_CC exprInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.expr", "expr", "VapourSynth Expr Filter", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Expr", "clips:clip[];expr:data[];format:int:opt;", exprCreate, nullptr, plugin);
}
