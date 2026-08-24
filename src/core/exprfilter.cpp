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
#include "gpufilter.h"
#include "VSVulkan4.h"
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

namespace {

/* A third backend for the same bytecode the interpreter and the x86 JIT already consume.
   expr::compile has resolved the stack by this point -- DUP, SWAP and MUX never reach an
   executable list -- so every instruction carries explicit register indices and the
   translation is one GLSL statement per instruction, in order.

   Booleans are the interpreter's convention throughout: 1.0 or 0.0 out, anything above zero
   is true going in, so the logical operators stay arithmetic rather than becoming bit
   masks. */
struct ExprGlsl {
    std::string body;
    int maxReg = 0;

    static std::string reg(int i) { return "r" + std::to_string(i); }

    void emit(const std::string &dst, const std::string &expression) {
        body += "        " + dst + " = " + expression + ";\n";
    }
};

/* Two builtins need help before they match the scalar reference, both semantic rather than
   last-bit questions.

   pow: the reference std::pow is defined for a negative base with an integral exponent, while
   the GLSL builtin is undefined for any negative base and returns NaN in practice, so the
   sign is carried around the call.

   sin and cos: Vulkan specifies their precision only inside [-pi, pi], and expressions
   routinely feed them raw sample values, so the argument is reduced first. That reduction is
   a single float32 step, coarser than either scalar backend -- the interpreter's std::sin is
   exact, the JIT uses a four constant Cody-Waite split (float_pi1..pi4 in
   jitcompiler_x86.cpp). Against the JIT at arguments up to 65535 radians it costs a mean
   absolute error of 7e-4 and a worst case of 5e-3, two samples per 1080p plane once quantised
   back to 16 bit. The same four piece pi would close it. */
const char exprHelpers[] =
    "float vsExprPow(float base, float e) {\n"
    "    if (base >= 0.0) return pow(base, e);\n"
    "    if (e != trunc(e)) return 0.0 / 0.0;\n"
    "    float m = pow(-base, e);\n"
    "    return mod(abs(e), 2.0) == 1.0 ? -m : m;\n"
    "}\n"
    "float vsExprReduce(float x) { return x - 6.28318530717958648 * round(x * 0.15915494309189535); }\n"
    "float vsExprSin(float x) { return sin(vsExprReduce(x)); }\n"
    "float vsExprCos(float x) { return cos(vsExprReduce(x)); }\n";

std::string exprPlaneBody(const std::vector<ExprInstruction> &code, int &maxReg) {
    ExprGlsl g;
    for (const ExprInstruction &insn : code) {
        maxReg = std::max(maxReg, insn.dst + 1);
        const std::string d = ExprGlsl::reg(insn.dst);
        const std::string a = ExprGlsl::reg(insn.src1);
        const std::string b = ExprGlsl::reg(insn.src2);
        const std::string c = ExprGlsl::reg(insn.src3);
        const std::string in = std::to_string(insn.op.imm.u);

        switch (insn.op.type) {
        case ExprOpType::MEM_LOAD_U8:
        case ExprOpType::MEM_LOAD_U16:
        case ExprOpType::MEM_LOAD_F32:
            g.emit(d, "float(s" + in + "[idx" + in + "])");
            break;
        case ExprOpType::MEM_LOAD_F16:
            g.emit(d, "float(s" + in + "[idx" + in + "])");
            break;
        case ExprOpType::CONSTANT: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.9g", insn.op.imm.f);
            std::string lit = buf;
            if (lit.find_first_of(".eE") == std::string::npos && lit.find("inf") == std::string::npos
                && lit.find("nan") == std::string::npos)
                lit += ".0";
            g.emit(d, lit);
            break;
        }
        case ExprOpType::ADD:  g.emit(d, a + " + " + b); break;
        case ExprOpType::SUB:  g.emit(d, a + " - " + b); break;
        case ExprOpType::MUL:  g.emit(d, a + " * " + b); break;
        case ExprOpType::DIV:  g.emit(d, a + " / " + b); break;
        case ExprOpType::FMA:
            switch (static_cast<FMAType>(insn.op.imm.u)) {
            case FMAType::FMADD:  g.emit(d, b + " * " + c + " + " + a); break;
            case FMAType::FMSUB:  g.emit(d, b + " * " + c + " - " + a); break;
            case FMAType::FNMADD: g.emit(d, "-(" + b + " * " + c + ") + " + a); break;
            case FMAType::FNMSUB: g.emit(d, "-(" + b + " * " + c + ") - " + a); break;
            }
            break;
        case ExprOpType::MAX:  g.emit(d, "max(" + a + ", " + b + ")"); break;
        case ExprOpType::MIN:  g.emit(d, "min(" + a + ", " + b + ")"); break;
        case ExprOpType::SQRT: g.emit(d, "vsSqrt(" + a + ")"); break;
        case ExprOpType::ABS:  g.emit(d, "abs(" + a + ")"); break;
        case ExprOpType::NEG:  g.emit(d, "-" + a); break;
        case ExprOpType::EXP:  g.emit(d, "exp(" + a + ")"); break;
        case ExprOpType::LOG:  g.emit(d, "log(" + a + ")"); break;
        case ExprOpType::POW:  g.emit(d, "vsExprPow(" + a + ", " + b + ")"); break;
        case ExprOpType::SIN:  g.emit(d, "vsExprSin(" + a + ")"); break;
        case ExprOpType::COS:  g.emit(d, "vsExprCos(" + a + ")"); break;
        case ExprOpType::CMP: {
            static const char *cmp[] = { "==", "<", "<=", "", "!=", ">=", ">" };
            g.emit(d, "float(" + a + " " + cmp[insn.op.imm.u] + " " + b + ")");
            break;
        }
        case ExprOpType::AND: g.emit(d, "float((" + a + " > 0.0) && (" + b + " > 0.0))"); break;
        case ExprOpType::OR:  g.emit(d, "float((" + a + " > 0.0) || (" + b + " > 0.0))"); break;
        case ExprOpType::XOR: g.emit(d, "float((" + a + " > 0.0) != (" + b + " > 0.0))"); break;
        case ExprOpType::NOT: g.emit(d, "float(!(" + a + " > 0.0))"); break;
        case ExprOpType::TERNARY:
            g.emit(d, "(" + a + " > 0.0) ? " + b + " : " + c);
            break;
        /* The store ends the plane; clamp_int rounds to nearest even into the storage type
           then caps at the format's maximum, which is the same two step the stencil filters
           already agree with. */
        case ExprOpType::MEM_STORE_U8:
            g.body += "        dstData[dstIdx] = SAMPLE_T(min(uint(clamp(roundEven(" + a + "), 0.0, 255.0)), 255u));\n";
            break;
        case ExprOpType::MEM_STORE_U16:
            g.body += "        dstData[dstIdx] = SAMPLE_T(min(uint(clamp(roundEven(" + a +
                      "), 0.0, 65535.0)), " + std::to_string((1u << insn.op.imm.u) - 1) + "u));\n";
            break;
        case ExprOpType::MEM_STORE_F16:
            /* Not a plain conversion: SPIR-V leaves 32 to 16 bit rounding implementation
               defined and at least one desktop driver truncates toward zero, where every
               scalar path here rounds to nearest even; see the note in gpufilter.h. */
            g.body += "        dstData[dstIdx] = SAMPLE_T(" + a + ");\n";
            break;
        case ExprOpType::MEM_STORE_F32:
            g.body += "        dstData[dstIdx] = SAMPLE_T(" + a + ");\n";
            break;
        default:
            return std::string(); /* an opcode this backend does not know: fall back */
        }
    }
    return g.body;
}

} // namespace

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

    ClipResidencyResult residency = { ClipResidency::AllCPU, 0 };

    try {
        int cpulevel = vs_get_cpulevel(core);
        /* Set by compile_jit only when the OS refused executable memory, which is the one
           reason for a missing proc that is worth telling the user about; an unsupported CPU
           level or the software half float path are expected and stay quiet. */
        bool execMemDenied = false;

        d->numInputs = vsapi->mapNumElements(in, "clips");
        if (d->numInputs > 26)
            throw std::runtime_error("More than 26 input clips provided");

        for (int i = 0; i < d->numInputs; i++) {
            d->node[i] = vsapi->mapGetNode(in, "clips", i, &err);
        }

        residency = residencyOfClips(d->node, d->numInputs, vsapi);

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

            if (!is8to16orFloatFormat(vi[i]->format))
                throw std::runtime_error(invalidVideoFormatMessage(vi[i]->format, vsapi, nullptr));
        }

        d->vi = *vi[0];
        int format = vsapi->mapGetIntSaturated(in, "format", 0, &err);
        if (!err) {
            VSVideoFormat f;
            if (!vsapi->getVideoFormatByID(&f, format, core) || f.colorFamily == cfUndefined)
                throw std::runtime_error("the format id specified in format is invalid");
            if (d->vi.format.colorFamily != f.colorFamily || d->vi.format.subSamplingW != f.subSamplingW || d->vi.format.subSamplingH != f.subSamplingH)
                throw std::runtime_error("the output format must have the same color family and subsampling as the input");
            if (d->vi.format.numPlanes != f.numPlanes)
                throw std::runtime_error("The number of planes in the inputs and output must match");
            vsapi->queryVideoFormat(&d->vi.format, f.colorFamily, f.sampleType, f.bitsPerSample, f.subSamplingW, f.subSamplingH, core);
        }

        if (!is8to16orFloatFormat(d->vi.format))
            throw std::runtime_error(invalidVideoFormatMessage(d->vi.format, vsapi, nullptr));

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

            /* A GPU graph builds its kernel from this bytecode and never calls the procs,
               so the JIT -- with its executable memory allocation and the execmem warning
               below -- is skipped outright rather than compiled and thrown away. */
            if (residency.kind == ClipResidency::AllGPU)
                continue;

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
                std::tie(d->proc[i], d->procSize[i]) = expr::compile_jit(d->bytecode[i].data(), d->bytecode[i].size(), d->numInputs, cpulevel, &d->procPixels[i], &execMemDenied);
        }

        /* The interpreter covers everything the JIT does, so being refused executable memory
           costs speed and nothing else -- but silently running an order of magnitude slower is
           worse than saying so, and the cause is one the user can actually fix. Warned once per
           filter rather than per plane, since all three planes fail the same way. */
        if (execMemDenied)
            vsapi->logMessage(mtWarning, "Expr: could not allocate executable memory for the compiled "
                "expression, falling back to the much slower interpreter. On Linux this is typically an "
                "SELinux policy denying execmem; see the boolean of that name.", core);
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

    /* Nothing else frees these: ~ExprData only unmaps the JIT code, and exprFree runs only
       for a filter that was actually created, so every path that gives up here has to
       return the input references itself. */
    auto freeNodes = [&]() {
        for (int i = 0; i < MAX_EXPR_INPUTS; i++)
            vsapi->freeNode(d->node[i]);
    };

    if (residency.kind == ClipResidency::Mixed) {
        freeNodes();
        vsapi->mapSetError(out, residencyMismatchError("Expr", residency.mixedAt).c_str());
        return;
    }

    if (residency.kind == ClipResidency::AllGPU) {
        /* One glsl source for the whole filter, with the plane bodies as branches on a
           specialization constant and one program per distinct body: the text is parsed
           once however many bodies there are, and each pipeline folds the other bodies
           away at creation. Branching on a push constant instead -- the previous shape --
           kept one pipeline but made every dispatch carry the register pressure of the
           heaviest plane's code, so a trivial chroma expression paid for an elaborate
           luma one. */
        std::string planes[3];
        int maxReg = 1;
        bool ok = true;
        for (int i = 0; i < d->vi.format.numPlanes; i++) {
            if (d->plane[i] == poProcess) {
                planes[i] = exprPlaneBody(d->bytecode[i], maxReg);
                ok = ok && !planes[i].empty();
            } else if (d->plane[i] == poUndefined) {
                /* The scalar path leaves these planes as the allocator found them. Writing
                   zeroes costs one trivial dispatch and makes the output reproducible,
                   which is worth more than matching uninitialised memory. */
                planes[i] = "        dstData[dstIdx] = SAMPLE_T(0);\n";
            }
        }
        if (!ok) {
            freeNodes();
            vsapi->mapSetError(out, "Expr: the expression uses an operation with no GPU kernel");
            return;
        }

        /* Planes sharing a body share a program, so the common case of one expression for
           every plane still builds a single pipeline. */
        std::vector<std::string> groupBodies;
        int planeGroup[3] = { -1, -1, -1 };
        for (int i = 0; i < d->vi.format.numPlanes; i++) {
            if (planes[i].empty())
                continue;
            for (size_t g = 0; g < groupBodies.size(); g++)
                if (groupBodies[g] == planes[i])
                    planeGroup[i] = static_cast<int>(g);
            if (planeGroup[i] < 0) {
                planeGroup[i] = static_cast<int>(groupBodies.size());
                groupBodies.push_back(planes[i]);
            }
        }

        const VSVideoFormat &of = d->vi.format;
        bool anyHalf = vsgpu::glslUsesFloat16(of);
        for (int i = 0; i < d->numInputs; i++)
            anyHalf = anyHalf || vsgpu::glslUsesFloat16(vsapi->getVideoInfo(d->node[i])->format);
        std::string src = "#version 460\n" + vsgpu::glslTypePreamble(anyHalf);
        src += std::string("#define SAMPLE_T ") + vsgpu::glslElementType(of) + "\n";
        src += "\nlayout(local_size_x = 16, local_size_y = 16) in;\n";

        /* Each input keeps its own sample type: Expr accepts clips of differing formats and
           the load opcode already records which width it wants. */
        for (int i = 0; i < d->numInputs; i++) {
            const VSVideoFormat &f = vsapi->getVideoInfo(d->node[i])->format;
            const char *t = vsgpu::glslElementType(f);
            src += "layout(std430, set = 0, binding = " + std::to_string(i) + ") readonly buffer Src" +
                   std::to_string(i) + " { " + t + " s" + std::to_string(i) + "[]; };\n";
        }
        src += "layout(std430, set = 0, binding = " + std::to_string(d->numInputs) +
               ") writeonly buffer Dst { SAMPLE_T dstData[]; };\n";
        src += "layout(constant_id = 0) const uint GROUP = 0u;\n";
        src += "layout(push_constant) uniform PC {\n"
               "    uint width, height, dstStride;\n"
               "    uint srcStride[" + std::to_string(MAX_EXPR_INPUTS) + "];\n"
               "} pc;\n\n";
        src += "float vsSqrt(float s) {\n"
               "    float y = sqrt(s);\n"
               "    if (!(y > 0.0) || isinf(y)) return y;\n"
               "    precise float r = fma(-y, y, s);\n"
               "    return y + r / (y + y);\n"
               "}\n";
        src += exprHelpers;
        src += "\nvoid main() {\n"
               "    uint x = gl_GlobalInvocationID.x;\n"
               "    uint y = gl_GlobalInvocationID.y;\n"
               "    if (x >= pc.width || y >= pc.height) return;\n"
               "    uint dstIdx = y * pc.dstStride + x;\n";
        for (int i = 0; i < d->numInputs; i++)
            src += "    uint idx" + std::to_string(i) + " = y * pc.srcStride[" + std::to_string(i) + "] + x;\n";
        for (int i = 1; i < maxReg; i++)
            src += "    float r" + std::to_string(i) + " = 0.0;\n";
        src += "    float r0 = 0.0;\n";
        for (size_t g = 0; g < groupBodies.size(); g++)
            src += "    if (GROUP == " + std::to_string(g) + "u) {\n" + groupBodies[g] + "    }\n";
        src += "}\n";

        struct ExprPush {
            uint32_t width, height, dstStride;
            uint32_t srcStride[MAX_EXPR_INPUTS];
        };
        static_assert(sizeof(ExprPush) <= 128, "must fit Vulkan's guaranteed 128 byte push constant minimum");

        vsgpu::FilterDesc desc;
        desc.vi = d->vi;
        for (int i = 0; i < d->numInputs; i++)
            desc.nodes.push_back(d->node[i]);
        /* Only poCopy is shared from the input; poUndefined is written by the zeroing body
           above, so it counts as processed like any other plane. */
        for (int i = 0; i < 3; i++)
            desc.process[i] = d->plane[i] != poCopy;

        for (size_t g = 0; g < groupBodies.size(); g++) {
            vsgpu::Program program;
            program.glsl = src; /* shared text: the shader cache parses it once */
            program.storageBufferCount = d->numInputs + 1;
            program.pushConstantBytes = sizeof(ExprPush);
            const uint32_t group = static_cast<uint32_t>(g);
            program.specData.resize(sizeof(group));
            std::memcpy(program.specData.data(), &group, sizeof(group));
            program.specEntries.push_back({ 0, 0, sizeof(uint32_t) });
            desc.programs.push_back(std::move(program));

            vsgpu::Pass pass;
            pass.program = static_cast<int>(g);
            for (int i = 0; i < d->numInputs; i++)
                pass.bindings.push_back(vsgpu::Operand::source(i));
            pass.bindings.push_back(vsgpu::Operand::output());
            for (int i = 0; i < 3; i++)
                pass.planes[i] = planeGroup[i] == static_cast<int>(g);
            /* Every pass reads only source planes and writes only its own planes, so the
               passes cannot observe each other and no barriers are needed between them. */
            pass.independent = true;
            desc.passes.push_back(std::move(pass));
        }

        const int numInputs = d->numInputs;
        desc.fillPush = [numInputs](const vsgpu::PassInfo &info, void *pushData) {
            ExprPush push = {};
            push.width = info.width;
            push.height = info.height;
            push.dstStride = info.dstStrideElements();
            for (int i = 0; i < numInputs && i < MAX_EXPR_INPUTS; i++)
                push.srcStride[i] = info.strideElements[i];
            std::memcpy(pushData, &push, sizeof(push));
        };

        std::string error;
        VSNode *result = vsgpu::createFilter("Expr", desc, deps.data(), d->numInputs, core, vsapi, error);
        for (int i = 0; i < d->numInputs; i++)
            d->node[i] = nullptr; /* consumed either way */
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("Expr: " + error).c_str());
        return;
    }

    vsapi->createVideoFilter(out, "Expr", &d->vi, exprGetFrame, exprFree, fmParallel, deps.data(), d->numInputs, d.get(), core);
    d.release();
}

} // namespace


//////////////////////////////////////////
// Init

void exprInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Expr", "clips:vnode[]:all;expr:data[];format:int:opt;", "clip:vnode:all;", exprCreate, nullptr, plugin);
}
