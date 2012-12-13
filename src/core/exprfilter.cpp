/*
* Copyright (c) 2012 Fredrik Mellbin
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

#include <QtCore/QtCore>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstddef>
#include <stdexcept>
#include "VapourSynth.h"
#include "VSHelper.h"
#include "exprfilter.h"

#define VS_X86

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
    opLoadSrc8, opLoadSrc16, opLoadSrcF, opLoadConst,
    opStore8, opStore16, opStoreF,
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

typedef struct {
    VSNodeRef *node[3];
    VSVideoInfo vi;
    std::vector<ExprOp> ops[3];
    int plane[3];
#ifdef VS_X86
    void *stack;
#else
    std::vector<float> stack;
#endif
} JitExprData;

extern "C" void vs_evaluate_expr_sse2(const void *exprs, const uint8_t **rwptrs, const intptr_t *ptroffsets, int numiterations, void *stack);

static void VS_CC exprInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    JitExprData *d = (JitExprData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC exprGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    JitExprData *d = (JitExprData *) * instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < 3; i++)
            if (d->node[i])
                vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src[3];
        for (int i = 0; i < 3; i++)
            if (d->node[i])
                src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);
            else
                src[i] = NULL;

        const VSFormat *fi = d->vi.format;
        int height = vsapi->getFrameHeight(src[0], 0);
        int width = vsapi->getFrameWidth(src[0], 0);
        int planes[3] = { 0, 1, 2 };
        const VSFrameRef *srcf[3] = { d->plane[0] != poCopy ? NULL : src[0], d->plane[1] != poCopy ? NULL : src[0], d->plane[2] != poCopy ? NULL : src[0] };
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, width, height, srcf, planes, src[0], core);

        const uint8_t *srcp[3];
        int src_stride[3];

#ifdef VS_X86

        intptr_t ptroffsets[4] = { d->vi.format->bytesPerSample * 8, 0, 0, 0 };

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->plane[plane] == poProcess) {
                for (int i = 0; i < 3; i++) {
                    if (d->node[i]) {
                        srcp[i] = vsapi->getReadPtr(src[i], plane);
                        src_stride[i] = vsapi->getStride(src[i], plane);
                        ptroffsets[i + 1] = vsapi->getFrameFormat(src[i])->bytesPerSample * 8;
                    } else {
                        srcp[i] = NULL;
                        src_stride[i] = 0;
                        ptroffsets[i + 1] = 0;
                    }
                }

                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(dst, plane);
                int w = vsapi->getFrameWidth(dst, plane);

                int niterations = (w + 7)/8;
                const ExprOp *ops = &d->ops[plane][0];
                void *stack = d->stack;
                for (int y = 0; y < h; y++) {
                    const uint8_t *rwptrs[4] = { dstp + dst_stride * y, srcp[0] + src_stride[0] * y, srcp[1] + src_stride[1] * y, srcp[2] + src_stride[2] * y };
                    vs_evaluate_expr_sse2(ops, rwptrs, ptroffsets, niterations, stack);
                }
            }
        }
        
#else

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->plane[plane] == poProcess) {
                for (int i = 0; i < 3; i++) {
                    if (d->node[i]) {
                        srcp[i] = vsapi->getReadPtr(src[i], plane);
                        src_stride[i] = vsapi->getStride(src[i], plane);
                    } else {
                        srcp[i] = NULL;
                        src_stride[i] = 0;
                    }
                }

                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src[0], plane);
                int w = vsapi->getFrameWidth(src[0], plane);
                const ExprOp *vops = &d->ops[plane][0]; 
                float *stack = &d->stack[0];
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
                                stacktop = ((const uint16_t *)srcp[vops[i].e.ival])[x];
                                ++si;
                                break;
                            case opLoadSrcF:
                                stack[si] = stacktop;
                                stacktop = ((const float *)srcp[vops[i].e.ival])[x];
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
                                stacktop = exp(stacktop);
                                break;
                            case opLog:
                                stacktop = log(stacktop);
                                break;
                            case opPow:
                                --si;
                                stacktop = pow(stack[si], stacktop);
                                break;
                            case opSqrt:
                                stacktop = sqrt(stacktop);
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
                                ((uint16_t *)dstp)[x] = std::max(0.0f, std::min(stacktop, 256*255.0f)) + 0.5f;
                                goto loopend;
                            case opStoreF:
                                ((float *)dstp)[x] = stacktop;
                                goto loopend;
                            }
                        }
                        loopend:;
                    }
                    dstp += dst_stride;
                    srcp[0] += src_stride[0];
                    srcp[1] += src_stride[1];
                    srcp[2] += src_stride[2];
                }
            }
        }
#endif
        for (int i = 0; i < 3; i++)
            vsapi->freeFrame(src[i]);
        return dst;
    }

    return 0;
}

static void VS_CC exprFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    JitExprData *d = (JitExprData *)instanceData;
    for (int i = 0; i < 3; i++)
        vsapi->freeNode(d->node[i]);
    delete d;
}

static SOperation getLoadOp(const VSVideoInfo *vi) {
    if (!vi)
        return opLoadSrcF;
    if (vi->format->sampleType == stInteger) {
        if (vi->format->bitsPerSample == 8)
            return opLoadSrc8;
        return opLoadSrc16;
    } else {
        return opLoadSrcF;
    }
}

static SOperation getStoreOp(const VSVideoInfo *vi) {
    if (!vi)
        return opLoadSrcF;
    if (vi->format->sampleType == stInteger) {
        if (vi->format->bitsPerSample == 8)
            return opStore8;
        return opStore16;
    } else {
        return opStoreF;
    }
}

#define LOAD_OP(op,v) do { ops.push_back(ExprOp(op, (v))); maxStackSize = std::max(++stackSize, maxStackSize); } while(0)
#define GENERAL_OP(op, req, dec) do { if (stackSize < req) throw std::runtime_error("Not enough elements on stack to perform operation " + tokens[i]); ops.push_back(ExprOp(op)); stackSize-=(dec); } while(0)
#define ONE_ARG_OP(op) GENERAL_OP(op, 1, 0)
#define TWO_ARG_OP(op) GENERAL_OP(op, 2, 1)
#define THREE_ARG_OP(op) GENERAL_OP(op, 3, 2)

static int parseExpression(const std::string &expr, std::vector<ExprOp> &ops, const SOperation loadOp[], const SOperation storeOp) {
    std::vector<std::string> tokens;
    split(tokens, expr, " ", split1::no_empties);

    int maxStackSize = 0;
    int stackSize = 0;

    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "+") {
            TWO_ARG_OP(opAdd);
        } else if (tokens[i] == "-")
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
        /*
        else if (tokens[i] == "pow")
            ONE_ARG_OP(opPow);
            */
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
            TWO_ARG_OP(opNeg);
        else if (tokens[i] == "dup")
            LOAD_OP(opDup, 0);
        else if (tokens[i] == "swap")
            GENERAL_OP(opSwap, 1, 0);
        else if (tokens[i] == "x")
            LOAD_OP(loadOp[0], 0);
        else if (tokens[i] == "y")
            LOAD_OP(loadOp[1], 1);
        else if (tokens[i] == "z")
            LOAD_OP(loadOp[2], 2);
        else {
            size_t conv = 0;
            try {
                bool ok;
                LOAD_OP(opLoadConst, QString::fromUtf8(tokens[i].c_str()).toFloat(&ok));
            } catch (std::logic_error) {
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float");
            }
            if (conv != tokens[i].length())
                throw std::runtime_error("Failed to convert '" + tokens[i] + "' to float");
        }
    }

    if (tokens.size() > 0) {
        if (stackSize != 1)
            throw std::runtime_error("Stack unbalanced at end of expression. Need to have exactly one value on the stack.");
        ops.push_back(storeOp);
    }

    return maxStackSize;
}

static void VS_CC exprCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    JitExprData d;
    JitExprData *data;
    int err;

    try {

        for (int i = 0; i < 3; i++)
            d.node[i] = vsapi->propGetNode(in, "clips", i, &err);

        const VSVideoInfo *vi[3];
        for (int i = 0; i < 3; i++)
            if (d.node[i])
                vi[i] = vsapi->getVideoInfo(d.node[i]);
            else
                vi[i] = NULL;

        for (int i = 0; i < 3; i++) {
            if (vi[i]) {
                if (!isConstantFormat(vi[i]))
                    throw std::runtime_error("Only constant format input allowed");
                if (vi[0]->format->numPlanes != vi[i]->format->numPlanes
                    || vi[0]->format->subSamplingW != vi[i]->format->subSamplingW
                    || vi[0]->format->subSamplingH != vi[i]->format->subSamplingH
                    || vi[0]->width != vi[i]->width
                    || vi[0]->height != vi[i]->height)
                    throw std::runtime_error("All inputs must have the same number of planes and the same dimensions, subsampling included");
                if ((vi[i]->format->bitsPerSample > 16 && vi[i]->format->sampleType == stInteger)
                    || vi[i]->format->bitsPerSample != 32 && vi[i]->format->sampleType == stFloat)
                    throw std::runtime_error("Input clips must be 8-16 bit integer or 32 bit float format");
            }
        }

        d.vi = *vi[0];
        int format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));
        if (!err) {
            const VSFormat *f = vsapi->getFormatPreset(format, core);
            if (f) {
                if (d.vi.format->colorFamily == cmCompat)
                    throw std::runtime_error("No compat formats allowed");
                if (d.vi.format->numPlanes != f->numPlanes)
                    throw std::runtime_error("The number of planes in the inputs and output must match");
                d.vi.format = vsapi->registerFormat(d.vi.format->colorFamily, f->sampleType, f->bitsPerSample, d.vi.format->subSamplingW, d.vi.format->subSamplingH, core);
            }
        }

        int nexpr = vsapi->propNumElements(in, "expr");
        if (nexpr > d.vi.format->numPlanes)
            throw std::runtime_error("More expressions given than there are planes");

        std::string expr[3];
        for (int i = 0; i < nexpr; i++)
            expr[i] = vsapi->propGetData(in, "expr", i, 0);
        if (nexpr == 1) {
            expr[1] = expr[0];
            expr[2] = expr[0];
        } else if (nexpr == 2) {
            expr[2] = expr[1];
        }

        for (int i = 0; i < 3; i++) {
            if (!expr[i].empty()) {
                d.plane[i] = poProcess;
            } else {
                if (d.vi.format->bitsPerSample == vi[0]->format->bitsPerSample && d.vi.format->sampleType == vi[0]->format->sampleType)
                    d.plane[i] = poCopy;
                else
                    d.plane[i] = poUndefined;
            }
        }

        const SOperation sop[3] = { getLoadOp(vi[0]), getLoadOp(vi[1]), getLoadOp(vi[2]) };
        int maxStackSize = 0;
        for (int i = 0; i < d.vi.format->numPlanes; i++)
            maxStackSize = std::max(parseExpression(expr[i], d.ops[i], sop, getStoreOp(&d.vi)), maxStackSize);

#ifdef VS_X86
        d.stack = vs_aligned_malloc<void>(maxStackSize * 32, 32);
#else
        d.stack.resize(maxStackSize);
#endif
    } catch (std::runtime_error &e) {
        for (int i = 0; i < 3; i++)
            vsapi->freeNode(d.node[i]);
        std::string s = "Expr: ";
        s += e.what();
        vsapi->setError(out, s.c_str());
        return;
    }

    data = new JitExprData();
    *data = d;

    vsapi->createFilter(in, out, "Expr", exprInit, exprGetFrame, exprFree, fmParallelRequests, 0, data, core);
}

//////////////////////////////////////////
// Init

void VS_CC exprInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.expr", "expr", "VapourSynth Expr Filter", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Expr", "clips:clip[];expr:data[];format:int:opt;", exprCreate, 0, plugin);
}
