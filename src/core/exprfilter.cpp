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
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "float16_helper.h"
#include "expr/expr.h"
#include "expr/jitcompiler.h"
#include "kernel/cpulevel.h"

#ifdef VS_TARGET_OS_WINDOWS
#include <windows.h>
#else
#include <sys/mman.h>
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
    ExprCompiler::ProcessLineProc proc[3];
    size_t procSize[3];
    int procPixels[3] = { 8, 8, 8 };  // pixels/iteration of the JIT proc (16 for the AVX-512 path)

    ExprData() : node(), vi(), plane(), numInputs(), proc() {}

    ~ExprData() {
        for (int i = 0; i < 3; i++) {
            if (proc[i]) {
#ifdef VS_TARGET_OS_WINDOWS
                VirtualFree((LPVOID)proc[i], 0, MEM_RELEASE);
#else
                munmap((void *)proc[i], procSize[i]);
#endif
            }
        }
    }
};

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
            case ExprOpType::MEM_LOAD_F16: DST = halfToFloat(reinterpret_cast<const uint16_t *>(srcp[insn.op.imm.u])[x]); break;
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
            case ExprOpType::MEM_STORE_F16: reinterpret_cast<uint16_t *>(dstp)[x] = floatToHalf(SRC1); return;
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
        alignas(32) intptr_t ptroffsets[((MAX_EXPR_INPUTS + 1) + 7) & ~7] = {};

        for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
            if (d->plane[plane] != poProcess)
                continue;

            // Pixels the compiled proc consumes per iteration (16 on the AVX-512 path,
            // 8 otherwise). Drives both the per-iteration pointer advance and the count.
            int lanes = d->procPixels[plane];
            ptroffsets[0] = d->vi.format.bytesPerSample * lanes;

            for (int i = 0; i < numInputs; i++) {
                srcp[i] = vsapi->getReadPtr(src[i], plane);
                src_stride[i] = vsapi->getStride(src[i], plane);
                ptroffsets[i + 1] = vsapi->getVideoFrameFormat(src[i])->bytesPerSample * lanes;
            }

            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(dst, plane);
            int w = vsapi->getFrameWidth(dst, plane);

            if (d->proc[plane]) {
                ExprCompiler::ProcessLineProc proc = d->proc[plane];
                int niterations = (w + lanes - 1) / lanes;

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

    // Half input/output is always accepted: the scalar interpreter handles it on any CPU
    // (via float16_helper). The JIT's half load/store use F16C (vcvtph2ps/vcvtps2ph), so
    // when the CPU lacks F16C we fall back to the interpreter per-plane (see below) rather
    // than rejecting half.
#ifdef VS_TARGET_CPU_X86
    const bool jitHasF16C = getCPUFeatures()->f16c;
#else
    const bool jitHasF16C = false;
#endif

    try {
        int cpulevel = vs_get_cpulevel(core);

        d->numInputs = vsapi->mapNumElements(in, "clips");
        if (d->numInputs > 26)
            throw std::runtime_error("More than 26 input clips provided");

        for (int i = 0; i < d->numInputs; i++) {
            d->node[i] = vsapi->mapGetNode(in, "clips", i, &err);
        }

        const VSVideoInfo *vi[MAX_EXPR_INPUTS] = {};
        for (int i = 0; i < d->numInputs; i++)
            vi[i] = vsapi->getVideoInfo(d->node[i]);

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

            if (!is8to16orFloatFormat(vi[i]->format, true))
                throw std::runtime_error(invalidVideoFormatMessage(vi[i]->format, vsapi, nullptr, true));
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

        if (!is8to16orFloatFormat(d->vi.format, true))
            throw std::runtime_error(invalidVideoFormatMessage(d->vi.format, vsapi, nullptr, true));

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

            // The JIT converts half via F16C; when that's missing, leave proc[i] null for
            // any plane that loads or stores half so exprGetFrame runs the interpreter for
            // it (which does the conversion in software) instead of emitting an illegal
            // vcvtph2ps.
            bool planeUsesHalf = false;
            if (!jitHasF16C) {
                for (const ExprInstruction &insn : d->bytecode[i]) {
                    if (insn.op.type == ExprOpType::MEM_LOAD_F16 || insn.op.type == ExprOpType::MEM_STORE_F16) {
                        planeUsesHalf = true;
                        break;
                    }
                }
            }

            if (cpulevel > VS_CPU_LEVEL_NONE && !planeUsesHalf)
                std::tie(d->proc[i], d->procSize[i]) = expr::compile_jit(d->bytecode[i].data(), d->bytecode[i].size(), d->numInputs, cpulevel, &d->procPixels[i]);
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
        deps.push_back({d->node[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->node[i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    vsapi->createVideoFilter(out, "Expr", &d->vi, exprGetFrame, exprFree, fmParallel, deps.data(), d->numInputs, d.get(), core);
    d.release();
}

} // namespace


//////////////////////////////////////////
// Init

void exprInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Expr", "clips:vnode[];expr:data[];format:int:opt;", "clip:vnode;", exprCreate, nullptr, plugin);
}
