/*
* Copyright (c) 2013-2020 Fredrik Mellbin
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

#ifndef JITCOMPILER_H
#define JITCOMPILER_H

#include <cstddef>
#include <memory>
#include "expr.h"

namespace expr {

class ExprCompiler {
public:
    typedef void (*ProcessLineProc)(void *rwptrs, intptr_t ptroff[MAX_EXPR_INPUTS + 1], intptr_t niter);
private:
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
    virtual ~ExprCompiler() = default;

    void addInstruction(const ExprInstruction &insn);

    virtual std::pair<ProcessLineProc, size_t> getCode() = 0;
};

#ifdef VS_TARGET_CPU_X86
std::unique_ptr<ExprCompiler> make_xmm_compiler(int numInputs);
std::unique_ptr<ExprCompiler> make_ymm_compiler(int numInputs);
#endif

std::pair<ExprCompiler::ProcessLineProc, size_t> compile_jit(const ExprInstruction *bytecode, size_t numInsns, int numInputs, int cpulevel);

} // namespace expr

#endif // JITCOMPILER_H
