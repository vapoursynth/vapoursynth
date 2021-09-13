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

#ifndef EXPR_H
#define EXPR_H

#include <cstdint>
#include <string>
#include <vector>

struct VSVideoInfo;

namespace expr {

#define MAX_EXPR_INPUTS 26

enum class ExprOpType {
    // Terminals.
    MEM_LOAD_U8, MEM_LOAD_U16, MEM_LOAD_F16, MEM_LOAD_F32, CONSTANT,
    MEM_STORE_U8, MEM_STORE_U16, MEM_STORE_F16, MEM_STORE_F32,

    // Arithmetic primitives.
    ADD, SUB, MUL, DIV, FMA, SQRT, ABS, NEG, MAX, MIN, CMP,

    // Logical operators.
    AND, OR, XOR, NOT,

    // Transcendental functions.
    EXP, LOG, POW, SIN, COS,

    // Ternary operator
    TERNARY,

    // Meta-node holding true/false branches of ternary.
    MUX,

    // Stack helpers.
    DUP, SWAP,
};

enum class FMAType {
    FMADD = 0,  // (b * c) + a
    FMSUB = 1,  // (b * c) - a
    FNMADD = 2, // -(b * c) + a
    FNMSUB = 3, // -(b * c) - a
};

enum class ComparisonType {
    EQ = 0,
    LT = 1,
    LE = 2,
    NEQ = 4,
    NLT = 5,
    NLE = 6,
};

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

inline bool operator==(const ExprOp &lhs, const ExprOp &rhs) { return lhs.type == rhs.type && lhs.imm.u == rhs.imm.u; }
inline bool operator!=(const ExprOp &lhs, const ExprOp &rhs) { return !(lhs == rhs); }

struct ExprInstruction {
    ExprOp op;
    int dst;
    int src1;
    int src2;
    int src3;

    ExprInstruction(ExprOp op) : op(op), dst(-1), src1(-1), src2(-1), src3(-1) {}
};

std::vector<ExprInstruction> compile(const std::string &expr, const VSVideoInfo * const srcFormats[], int numInputs, const VSVideoInfo &dstFormat, bool optimize = true);

} // namespace expr

#endif // EXPR_H
