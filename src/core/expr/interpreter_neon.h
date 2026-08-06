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

#ifndef EXPR_INTERPRETER_NEON_H
#define EXPR_INTERPRETER_NEON_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include "expr.h"

namespace expr {

// Evaluates Expr bytecode a vector of pixels at a time using NEON, for targets
// with no JIT backend. Same bytecode and same results as the scalar interpreter
// in exprfilter.cpp, but the opcode dispatch is amortized over the vector rather
// than repeated per pixel.
//
// Only whole vectors are handled: processRow() returns how many pixels it
// consumed, and the caller finishes the row with the scalar interpreter. No NEON
// type appears here, so this header is safe to include unconditionally.
class NeonInterpreter {
    const ExprInstruction *bytecode;
    size_t numInsns;
    std::vector<float> registers;
public:
    NeonInterpreter(const ExprInstruction *bytecode, size_t numInsns);

    // Pixels evaluated per iteration.
    static int pixelsPerIteration();

    // Evaluates the leading (w / pixelsPerIteration()) * pixelsPerIteration()
    // pixels of one row and returns that count.
    int processRow(const uint8_t * const *srcp, uint8_t *dstp, int w);
};

} // namespace expr

#endif // EXPR_INTERPRETER_NEON_H
