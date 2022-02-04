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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "VapourSynth4.h"
#include "expr.h"

namespace expr {
namespace {

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
    void preorder(T visitor)
    {
        if (visitor(*this))
            return;

        if (left)
            left->preorder(visitor);
        if (right)
            right->preorder(visitor);
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

bool equalSubTree(const ExpressionTreeNode *lhs, const ExpressionTreeNode *rhs)
{
    if (lhs->valueNum >= 0 && rhs->valueNum >= 0)
        return lhs->valueNum == rhs->valueNum;
    if (lhs->op.type != rhs->op.type || lhs->op.imm.u != rhs->op.imm.u)
        return false;
    if (!!lhs->left != !!rhs->left || !!lhs->right != !!rhs->right)
        return false;
    if (lhs->left && !equalSubTree(lhs->left, rhs->left))
        return false;
    if (lhs->right && !equalSubTree(lhs->right, rhs->right))
        return false;
    return true;
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
        { "sin",  { ExprOpType::SIN } },
        { "cos",  { ExprOpType::COS } },
        { "dup",  { ExprOpType::DUP, 0 } },
        { "swap", { ExprOpType::SWAP, 1 } },
    };

    auto it = simple.find(token);
    if (it != simple.end()) {
        return it->second;
    } else if (token.size() == 1 && token[0] >= 'a' && token[0] <= 'z') {
        return{ ExprOpType::MEM_LOAD_U8, token[0] >= 'x' ? token[0] - 'x' : token[0] - 'a' + 3 };
    } else if (token.substr(0, 3) == "dup" || token.substr(0, 4) == "swap") {
        size_t prefix = token[0] == 'd' ? 3 : 4;
        size_t count = 0;
        int idx = -1;

        try {
            idx = std::stoi(token.substr(prefix), &count);
        } catch (...) {
            // ...
        }

        if (idx < 0 || prefix + count != token.size())
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

ExpressionTree parseExpr(const std::string &expr, const VSVideoInfo * const srcFormats[], int numInputs)
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
        3, // FMA
        1, // SQRT
        1, // ABS
        1, // NEG
        2, // MAX
        2, // MIN
        2, // CMP
        2, // AND
        2, // OR
        2, // XOR
        1, // NOT
        1, // EXP
        1, // LOG
        2, // POW
        1, // SIN
        1, // COS
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
            const VSVideoFormat &format = srcFormats[op.imm.i]->format;

            if (format.sampleType == stInteger && format.bytesPerSample == 1)
                op.type = ExprOpType::MEM_LOAD_U8;
            else if (format.sampleType == stInteger && format.bytesPerSample == 2)
                op.type = ExprOpType::MEM_LOAD_U16;
            else if (format.sampleType == stFloat && format.bytesPerSample == 2)
                op.type = ExprOpType::MEM_LOAD_F16;
            else if (format.sampleType == stFloat && format.bytesPerSample == 4)
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

bool isConstantExpr(const ExpressionTreeNode &node)
{
    switch (node.op.type) {
    case ExprOpType::MEM_LOAD_U8:
    case ExprOpType::MEM_LOAD_U16:
    case ExprOpType::MEM_LOAD_F16:
    case ExprOpType::MEM_LOAD_F32:
        return false;
    case ExprOpType::CONSTANT:
        return true;
    default:
        return (!node.left || isConstantExpr(*node.left)) && (!node.right || isConstantExpr(*node.right));
    }
}

bool isConstant(const ExpressionTreeNode &node)
{
    return node.op.type == ExprOpType::CONSTANT;
}

bool isConstant(const ExpressionTreeNode &node, float val)
{
    return node.op.type == ExprOpType::CONSTANT && node.op.imm.f == val;
}

float evalConstantExpr(const ExpressionTreeNode &node)
{
    auto bool2float = [](bool x) { return x ? 1.0f : 0.0f; };
    auto float2bool = [](float x) { return x > 0.0f; };

#define LEFT evalConstantExpr(*node.left)
#define RIGHT evalConstantExpr(*node.right)
#define RIGHTLEFT evalConstantExpr(*node.right->left)
#define RIGHTRIGHT evalConstantExpr(*node.right->right)
    switch (node.op.type) {
    case ExprOpType::CONSTANT: return node.op.imm.f;
    case ExprOpType::ADD: return LEFT + RIGHT;
    case ExprOpType::SUB: return LEFT - RIGHT;
    case ExprOpType::MUL: return LEFT * RIGHT;
    case ExprOpType::DIV: return LEFT / RIGHT;
    case ExprOpType::FMA:
        switch (static_cast<FMAType>(node.op.imm.u)) {
        case FMAType::FMADD: return RIGHTLEFT * RIGHTRIGHT + LEFT;
        case FMAType::FMSUB: return RIGHTLEFT * RIGHTRIGHT - LEFT;
        case FMAType::FNMADD: return -(RIGHTLEFT * RIGHTRIGHT) + LEFT;
        case FMAType::FNMSUB: return -(RIGHTLEFT * RIGHTRIGHT) - LEFT;
        }
        return NAN;
    case ExprOpType::SQRT: return std::sqrt(LEFT);
    case ExprOpType::ABS: return std::fabs(LEFT);
    case ExprOpType::NEG: return -LEFT;
    case ExprOpType::MAX: return std::max(LEFT, RIGHT);
    case ExprOpType::MIN: return std::min(LEFT, RIGHT);
    case ExprOpType::CMP:
        switch (static_cast<ComparisonType>(node.op.imm.u)) {
        case ComparisonType::EQ: return bool2float(LEFT == RIGHT);
        case ComparisonType::LT: return bool2float(LEFT < RIGHT);
        case ComparisonType::LE: return bool2float(LEFT <= RIGHT);
        case ComparisonType::NEQ: return bool2float(LEFT != RIGHT);
        case ComparisonType::NLT: return bool2float(LEFT >= RIGHT);
        case ComparisonType::NLE: return bool2float(LEFT > RIGHT);
        }
        return NAN;
    case ExprOpType::AND: return bool2float(float2bool(LEFT) && float2bool(RIGHT));
    case ExprOpType::OR: return bool2float(float2bool(LEFT) || float2bool(RIGHT));
    case ExprOpType::XOR: return bool2float(float2bool(LEFT) != float2bool(RIGHT));
    case ExprOpType::NOT: return bool2float(!float2bool(LEFT));
    case ExprOpType::EXP: return std::exp(LEFT);
    case ExprOpType::LOG: return std::log(LEFT);
    case ExprOpType::POW: return std::pow(LEFT, RIGHT);
    case ExprOpType::SIN: return std::sin(LEFT);
    case ExprOpType::COS: return std::cos(LEFT);
    case ExprOpType::TERNARY: return float2bool(LEFT) ? RIGHTLEFT : RIGHTRIGHT;
    default: return NAN;
    }
#undef RIGHTRIGHT
#undef RIGHTLEFT
#undef RIGHT
#undef LEFT
}

bool isOpCode(const ExpressionTreeNode &node, std::initializer_list<ExprOpType> types)
{
    for (ExprOpType type : types) {
        if (node.op.type == type)
            return true;
    }
    return false;
}

bool isInteger(float x)
{
    return std::floor(x) == x;
}

void replaceNode(ExpressionTreeNode &node, const ExpressionTreeNode &replacement)
{
    node.op = replacement.op;
    node.setLeft(replacement.left);
    node.setRight(replacement.right);
}

void swapNodeContents(ExpressionTreeNode &lhs, ExpressionTreeNode &rhs)
{
    std::swap(lhs, rhs);
    std::swap(lhs.parent, rhs.parent);
}

void applyValueNumbering(ExpressionTree &tree)
{
    std::vector<ExpressionTreeNode *> numbered;
    int valueNum = 0;

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        node.valueNum = -1;
    });

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op.type == ExprOpType::MUX)
            return;

        for (ExpressionTreeNode *testnode : numbered) {
            if (equalSubTree(&node, testnode)) {
                node.valueNum = testnode->valueNum;
                return;
            }
        }

        node.valueNum = valueNum++;
        numbered.push_back(&node);
    });
}

ExpressionTreeNode *emitIntegerPow(ExpressionTree &tree, const ExpressionTreeNode &node, int exponent)
{
    if (exponent == 1)
        return tree.clone(&node);

    ExpressionTreeNode *mulNode = tree.makeNode({ ExprOpType::MUL });
    mulNode->setLeft(emitIntegerPow(tree, node, (exponent + 1) / 2));
    mulNode->setRight(emitIntegerPow(tree, node, exponent - (exponent + 1) / 2));
    return mulNode;
}

typedef std::unordered_map<int, const ExpressionTreeNode *> ValueIndex;

class ExponentMap {
    struct CanonicalCompare {
        const ValueIndex &index;

        bool operator()(const std::pair<int, float> &lhs, const std::pair<int, float> &rhs) const
        {
            const std::initializer_list<ExprOpType> memOpCodes = { ExprOpType::MEM_LOAD_U8, ExprOpType::MEM_LOAD_U16, ExprOpType::MEM_LOAD_F16, ExprOpType::MEM_LOAD_F32 };

            // Order equivalent terms by exponent.
            if (lhs.first == rhs.first)
                return lhs.second < rhs.second;

            const ExpressionTreeNode *lhsNode = index.at(lhs.first);
            const ExpressionTreeNode *rhsNode = index.at(rhs.first);

            // Ordering: complex values, memory, constants
            int lhsCategory = isConstant(*lhsNode) ? 2 : isOpCode(*lhsNode, memOpCodes) ? 1 : 0;
            int rhsCategory = isConstant(*rhsNode) ? 2 : isOpCode(*rhsNode, memOpCodes) ? 1 : 0;

            if (lhsCategory != rhsCategory)
                return lhsCategory < rhsCategory;

            // Ordering criteria for each category:
            //
            // constants: order by value
            // memory: order by variable name
            // other: order by value number (unstable)
            if (lhsCategory == 2)
                return lhsNode->op.imm.f < rhsNode->op.imm.f;
            else if (lhsCategory == 1)
                return lhsNode->op.imm.u < rhsNode->op.imm.u;
            else
                return lhs.first < rhs.first;
        };
    };

    // e.g. 3 * v0^2 * v1^3
    // map = { 0: 2, 1: 3 }, coeff = 3
    std::map<int, float> map; // key = valueNum, value = exponent
    std::vector<int> origSequence;
    float coeff;

    bool expandOrigSequence(ValueIndex &index)
    {
        bool changed = false;

        for (size_t i = 0; i < origSequence.size(); ++i) {
            const ExpressionTreeNode *value = index.at(origSequence[i]);

            if (value->op == ExprOpType::POW && isConstant(*value->right)) {
                origSequence[i] = value->left->valueNum;
                changed = true;
            } else if (value->op == ExprOpType::MUL || value->op == ExprOpType::DIV) {
                origSequence[i] = value->left->valueNum;
                origSequence.insert(origSequence.begin() + i + 1, value->right->valueNum);
                changed = true;
            }
        }

        return changed;
    }

    bool expandOnePass(ValueIndex &index)
    {
        bool changed = false;

        for (auto it = map.begin(); it != map.end();) {
            const ExpressionTreeNode *value = index.at(it->first);
            bool erase = false;

            if (value->op == ExprOpType::POW && isConstant(*value->right)) {
                index[value->left->valueNum] = value->left;

                map[value->left->valueNum] += it->second * value->right->op.imm.f;
                erase = true;
            } else if (value->op == ExprOpType::MUL) {
                index[value->left->valueNum] = value->left;
                index[value->right->valueNum] = value->right;

                map[value->left->valueNum] += it->second;
                map[value->right->valueNum] += it->second;
                erase = true;
            } else if (value->op == ExprOpType::DIV) {
                index[value->left->valueNum] = value->left;
                index[value->right->valueNum] = value->right;

                map[value->left->valueNum] += it->second;
                map[value->right->valueNum] -= it->second;
                erase = true;
            }

            if (erase) {
                it = map.erase(it);
                changed = true;
                continue;
            }

            ++it;
        }

        return changed;
    }

    void combineConstants(const ValueIndex &index)
    {
        for (auto it = map.begin(); it != map.end();) {
            const ExpressionTreeNode *node = index.at(it->first);
            if (isConstant(*node)) {
                coeff *= std::pow(node->op.imm.f, it->second);
                it = map.erase(it);
                continue;
            }
            ++it;
        }
    }
public:
    ExponentMap() : coeff(1.0f) {}

    void addTerm(int valueNum, float exp)
    {
        map[valueNum] += exp;
        origSequence.push_back(valueNum);
    }

    void addCoeff(float val) { coeff += val; }

    void mulCoeff(float val) { coeff *= val; }

    float getCoeff() const { return coeff; }

    bool isScalar() const { return map.empty(); }

    size_t numTerms() const { return map.size() + (coeff != 1.0f ? 1 : 0); }

    bool isSameTerm(const ExponentMap &other) const
    {
        auto it1 = map.begin();
        auto it2 = other.map.begin();

        while (it1 != map.end() && it2 != other.map.end()) {
            if (it1->first != it2->first || it1->second != it2->second)
                return false;

            ++it1;
            ++it2;
        }

        return it1 == map.end() && it2 == other.map.end();
    }

    void expand(ValueIndex &index)
    {
        while (expandOnePass(index)) {
            // ...
        }
        combineConstants(index);

        while (expandOrigSequence(index)) {
            // ...
        }
    }

    bool isCanonical(const ValueIndex &index) const
    {
        std::vector<std::pair<int, float>> tmp;
        for (int x : origSequence) {
            tmp.push_back({ x, 1.0f });
        }
        return std::is_sorted(tmp.begin(), tmp.end(), CanonicalCompare{ index });
    }

    ExpressionTreeNode *emit(ExpressionTree &tree, const ValueIndex &index) const
    {
        std::vector<std::pair<int, float>> flat(map.begin(), map.end());
        std::sort(flat.begin(), flat.end(), CanonicalCompare{ index });

        ExpressionTreeNode *node = nullptr;

        for (auto &term : flat) {
            ExpressionTreeNode *powNode;

            if (term.second != 1.0f) {
                powNode = tree.makeNode(ExprOpType::POW);
                powNode->setLeft(tree.clone(index.at(term.first)));
                powNode->setRight(tree.makeNode({ ExprOpType::CONSTANT, term.second }));
            } else {
                powNode = tree.clone(index.at(term.first));
            }

            if (node) {
                ExpressionTreeNode *mulNode = tree.makeNode(ExprOpType::MUL);
                mulNode->setLeft(node);
                mulNode->setRight(powNode);
                node = mulNode;
            } else {
                node = powNode;
            }
        }

        if (node) {
            if (coeff != 1.0f) {
                ExpressionTreeNode *mulNode = tree.makeNode(ExprOpType::MUL);
                mulNode->setLeft(node);
                mulNode->setRight(tree.makeNode({ ExprOpType::CONSTANT, coeff }));
                node = mulNode;
            }
        } else {
            node = tree.makeNode({ ExprOpType::CONSTANT, coeff });
        }

        return node;
    }

    bool canonicalOrder(const ExponentMap &other, const ValueIndex &index) const
    {
        // Convert map to flat array, as canonical order is different from value numbering.
        std::vector<std::pair<int, float>> lhsFlat(map.begin(), map.end());
        std::vector<std::pair<int, float>> rhsFlat(other.map.begin(), other.map.end());

        CanonicalCompare pred{ index };
        std::sort(lhsFlat.begin(), lhsFlat.end(), pred);
        std::sort(rhsFlat.begin(), rhsFlat.end(), pred);
        return std::lexicographical_compare(lhsFlat.begin(), lhsFlat.end(), rhsFlat.begin(), rhsFlat.end(), pred);
    }
};

class AdditiveSequence {
    std::vector<ExponentMap> terms;
    float scalarTerm;
public:
    AdditiveSequence() : scalarTerm() {}

    void addTerm(int valueNum, int sign)
    {
        ExponentMap map;
        map.addTerm(valueNum, 1.0f);
        map.mulCoeff(static_cast<float>(sign));
        terms.push_back(std::move(map));
    }

    size_t numTerms() const { return terms.size() + (scalarTerm != 0.0f ? 1 : 0); }

    void expand(ValueIndex &index)
    {
        for (auto &term : terms) {
            term.expand(index);
        }

        for (auto it = terms.begin(); it != terms.end();) {
            if (it->isScalar()) {
                scalarTerm += it->getCoeff();
                it = terms.erase(it);
                continue;
            }

            ++it;
        }

        for (auto it1 = terms.begin(); it1 != terms.end();) {
            for (auto it2 = it1 + 1; it2 != terms.end(); ++it2) {
                if (it1->isSameTerm(*it2)) {
                    it1->addCoeff(it2->getCoeff());
                    it2->mulCoeff(0.0f);
                }
            }

            if (it1->getCoeff() == 0.0f) {
                it1 = terms.erase(it1);
                continue;
            }

            ++it1;
        }
    }

    bool canonicalize(const ValueIndex &index)
    {
        auto pred = [&](const ExponentMap &lhs, const ExponentMap &rhs)
        {
            return lhs.canonicalOrder(rhs, index);
        };

        if (std::is_sorted(terms.begin(), terms.end(), pred))
            return true;

        std::sort(terms.begin(), terms.end(), pred);
        return false;
    }

    ExpressionTreeNode *emit(ExpressionTree &tree, const ValueIndex &index) const
    {
        ExpressionTreeNode *head = nullptr;

        for (const auto &term : terms) {
            ExpressionTreeNode *node = term.emit(tree, index);

            if (head) {
                ExpressionTreeNode *addNode = tree.makeNode(ExprOpType::ADD);
                addNode->setLeft(head);
                addNode->setRight(node);
                head = addNode;
            } else {
                head = node;
            }
        }

        if (head) {
            if (scalarTerm != 0.0f) {
                ExpressionTreeNode *addNode = tree.makeNode(scalarTerm < 0 ? ExprOpType::SUB : ExprOpType::ADD);
                addNode->setLeft(head);
                addNode->setRight(tree.makeNode({ ExprOpType::CONSTANT, std::fabs(scalarTerm) }));
                head = addNode;
            }
        } else {
            head = tree.makeNode({ ExprOpType::CONSTANT, 0.0f });
        }

        return head;
    }
};

bool analyzeAdditiveExpression(ExpressionTree &tree, ExpressionTreeNode &node)
{
    size_t origNumTerms = 0;
    AdditiveSequence expr;
    ValueIndex index;

    node.preorder([&](ExpressionTreeNode &node)
    {
        if (isOpCode(node, { ExprOpType::ADD, ExprOpType::SUB }))
            return false;

        // Deduce net sign of term.
        const ExpressionTreeNode *parent = node.parent;
        const ExpressionTreeNode *cur = &node;
        int polarity = 1;

        while (parent && isOpCode(*parent, { ExprOpType::ADD, ExprOpType::SUB })) {
            if (parent->op == ExprOpType::SUB && cur == parent->right)
                polarity = -polarity;

            cur = parent;
            parent = parent->parent;
        }

        ++origNumTerms;
        expr.addTerm(node.valueNum, polarity);
        index[node.valueNum] = &node;
        return true;
    });

    expr.expand(index);
    bool canonical = expr.canonicalize(index);

    if (expr.numTerms() < origNumTerms || !canonical) {
        ExpressionTreeNode *seq = expr.emit(tree, index);
        replaceNode(node, *seq);
        return true;
    }

    return false;
}

bool analyzeMultiplicativeExpression(ExpressionTree &tree, ExpressionTreeNode &node)
{
    std::unordered_map<int, const ExpressionTreeNode *> index;

    ExponentMap expr;
    size_t origNumTerms = 0;
    size_t numDivs = 0;

    node.preorder([&](ExpressionTreeNode &node)
    {
        if (node.op == ExprOpType::DIV)
            ++numDivs;

        if (isOpCode(node, { ExprOpType::MUL, ExprOpType::DIV }))
            return false;

        // Deduce net sign of term.
        const ExpressionTreeNode *parent = node.parent;
        const ExpressionTreeNode *cur = &node;
        int polarity = 1;

        while (parent && isOpCode(*parent, { ExprOpType::MUL, ExprOpType::DIV })) {
            if (parent->op == ExprOpType::DIV && cur == parent->right)
                polarity = -polarity;

            cur = parent;
            parent = parent->parent;
        }

        expr.addTerm(node.valueNum, static_cast<float>(polarity));
        index[node.valueNum] = &node;
        ++origNumTerms;
        return true;
    });

    expr.expand(index);

    if (expr.numTerms() < origNumTerms || !expr.isCanonical(index) || numDivs) {
        ExpressionTreeNode *seq = expr.emit(tree, index);
        replaceNode(node, *seq);
        return true;
    }

    return false;
}

bool combinePowerTerms(ExpressionTree &tree)
{
    bool changed = false;

    applyValueNumbering(tree);

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        bool changed = false;

        // sqrt(x) = x ** 0.5
        if (node.op == ExprOpType::SQRT) {
            node.op = ExprOpType::POW;
            node.setRight(tree.makeNode({ ExprOpType::CONSTANT, 0.5f }));
            changed = true;
        }

        // (a ** b) * a = a ** (b + 1)
        if (node.op == ExprOpType::MUL && node.left->op == ExprOpType::POW && node.left->left->valueNum == node.right->valueNum) {
            replaceNode(node, *node.left);
            ExpressionTreeNode *tmp = node.right;
            node.right = tree.makeNode(ExprOpType::ADD);
            node.right->left = tmp;
            node.right->right = tree.makeNode({ ExprOpType::CONSTANT, 1.0f });
            changed = true;
        }

        // (a ** b) * (a ** c) = a ** (b + c)
        if (node.op == ExprOpType::MUL && node.left->op == ExprOpType::POW && node.right->op == ExprOpType::POW && node.left->left->valueNum == node.right->left->valueNum) {
            ExpressionTreeNode *lhs = node.left->right;
            ExpressionTreeNode *rhs = node.right->right;
            replaceNode(node, *node.left);
            node.right = tree.makeNode(ExprOpType::ADD);
            node.right->left = lhs;
            node.right->right = rhs;
            changed = true;
        }

        return changed;
    });

    return changed;
}

bool applyAlgebraicOptimizations(ExpressionTree &tree)
{
    bool changed = false;

    applyValueNumbering(tree);

    tree.getRoot()->preorder([&](ExpressionTreeNode &node)
    {
        if (isOpCode(node, { ExprOpType::ADD, ExprOpType::SUB }) && (!node.parent || !isOpCode(*node.parent, { ExprOpType::ADD, ExprOpType::SUB }))) {
            changed = changed || analyzeAdditiveExpression(tree, node);
            return changed;
        }

        if (isOpCode(node, { ExprOpType::MUL, ExprOpType::DIV }) && (!node.parent || !isOpCode(*node.parent, { ExprOpType::MUL, ExprOpType::DIV }))) {
            changed = changed || analyzeMultiplicativeExpression(tree, node);
            return changed;
        }

        return false;
    });

    return changed;
}

bool applyComparisonOptimizations(ExpressionTree &tree)
{
    bool changed = false;

    applyValueNumbering(tree);

    tree.getRoot()->preorder([&](ExpressionTreeNode &node)
    {
        // Eliminate constant conditions.
        if (node.op.type == ExprOpType::CMP && node.left->valueNum == node.right->valueNum) {
            ComparisonType type = static_cast<ComparisonType>(node.op.imm.u);
            if (type == ComparisonType::EQ || type == ComparisonType::LE || type == ComparisonType::NLT)
                replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 1.0f } });
            else
                replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 0.0f } });

            changed = true;
            return changed;
        }

        // Eliminate identical branches.
        if (node.op == ExprOpType::TERNARY && node.right->left->valueNum == node.right->right->valueNum) {
            replaceNode(node, *node.right->left);
            changed = true;
            return changed;
        }

        // MIN/MAX detection.
        if (node.op == ExprOpType::TERNARY && node.left->op.type == ExprOpType::CMP) {
            ComparisonType type = static_cast<ComparisonType>(node.left->op.imm.u);
            int cmpTerms[2] = { node.left->left->valueNum, node.left->right->valueNum };
            int muxTerms[2] = { node.right->left->valueNum, node.right->right->valueNum };

            bool isSameTerms = (cmpTerms[0] == muxTerms[0] && cmpTerms[1] == muxTerms[1]) || (cmpTerms[0] == muxTerms[1] && cmpTerms[1] == muxTerms[0]);
            bool isLessOrGreater = type == ComparisonType::LT || type == ComparisonType::LE || type == ComparisonType::NLE || type == ComparisonType::NLT;

            if (isSameTerms && isLessOrGreater) {
                // a < b ? a : b --> min(a, b)     a > b ? b : a --> min(a, b)
                // a > b ? a : b --> max(a, b)     a < b ? b : a --> max(a, b)
                bool min = (type == ComparisonType::LT || type == ComparisonType::LE) ? cmpTerms[0] == muxTerms[0] : cmpTerms[0] != muxTerms[0];
                ExpressionTreeNode *a = node.left->left;
                ExpressionTreeNode *b = node.left->right;

                replaceNode(node, ExpressionTreeNode{ min ? ExprOpType::MIN : ExprOpType::MAX });
                node.setLeft(a);
                node.setRight(b);

                changed = true;
                return changed;
            }
        }

        // CMP to SUB conversion. It has lower priority than other comparison transformations.
        if (node.op.type == ExprOpType::CMP && node.parent && isOpCode(*node.parent, { ExprOpType::AND, ExprOpType::OR, ExprOpType::XOR, ExprOpType::TERNARY })) {
            ComparisonType type = static_cast<ComparisonType>(node.op.imm.u);

            // a < b --> b - a    a > b --> a - b
            if (type == ComparisonType::LT || type == ComparisonType::NLE) {
                if (type == ComparisonType::LT)
                    std::swap(node.left, node.right);

                node.op = ExprOpType::SUB;
                changed = true;
                return changed;
            }
        }

        return false;
    });

    return changed;
}

bool applyLocalOptimizations(ExpressionTree &tree)
{
    bool changed = false;

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op.type == ExprOpType::MUX)
            return;

        // Constant folding.
        if (node.op.type != ExprOpType::CONSTANT && isConstantExpr(node)) {
            float val = evalConstantExpr(node);
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, val } });
            changed = true;
        }

        // Move constants to right-hand side to simplify identities.
        if (isOpCode(node, { ExprOpType::ADD, ExprOpType::MUL }) && isConstant(*node.left) && !isConstant(*node.right)) {
            std::swap(node.left, node.right);
            changed = true;
        }

        // x + 0 = x    x - 0 = x
        if (isOpCode(node, { ExprOpType::ADD, ExprOpType::SUB }) && isConstant(*node.right, 0.0f)) {
            replaceNode(node, *node.left);
            changed = true;
        }

        // x * 0 = 0    0 / x = 0
        if ((node.op == ExprOpType::MUL && isConstant(*node.right, 0.0f)) || (node.op == ExprOpType::DIV && isConstant(*node.left, 0.0f))) {
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 0.0f } });
            changed = true;
        }

        // x * 1 = x    x / 1 = x
        if (isOpCode(node, { ExprOpType::MUL, ExprOpType::DIV }) && isConstant(*node.right, 1.0f)) {
            replaceNode(node, *node.left);
            changed = true;
        }

        // log(exp(x)) = x    exp(log(x)) = x
        if ((node.op == ExprOpType::LOG && node.left->op == ExprOpType::EXP) || (node.op == ExprOpType::EXP && node.left->op == ExprOpType::LOG)) {
            replaceNode(node, *node.left->left);
            changed = true;
        }

        // x ** 0 = 1
        if (node.op == ExprOpType::POW && isConstant(*node.right, 0.0f)) {
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 1.0f } });
            changed = true;
        }

        // x ** 1 = x
        if (node.op == ExprOpType::POW && isConstant(*node.right, 1.0f)) {
            replaceNode(node, *node.left);
            changed = true;
        }

        // 0 ** x == 0
        if (node.op == ExprOpType::POW && isConstant(*node.left, 0.0f)) {
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 0.0f} });
            changed = true;
        }

        // 1 ** x == 1
        if (node.op == ExprOpType::POW && isConstant(*node.left, 1.0f)) {
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::CONSTANT, 1.0f} });
            changed = true;
        }

        // (a ** b) ** c = a ** (b * c)
        if (node.op == ExprOpType::POW && node.left->op == ExprOpType::POW && isConstant(*node.left->right) && isConstant(*node.right))
        {
            float exp_first = node.left->right->op.imm.f;
            float exp_second = node.right->op.imm.f;

            // Exponentiation to even power eliminates sign. Exponential by non-integer implies non-negative base.
            if (isInteger(exp_first) && static_cast<int>(exp_first) % 2 == 0 && !isInteger(exp_second)) {
                ExpressionTreeNode *base = node.left->left;
                node.setLeft(tree.makeNode(ExprOpType::ABS));
                node.left->setLeft(base);
                node.setRight(tree.makeNode({ ExprOpType::CONSTANT, exp_first * exp_second }));
            } else {
                replaceNode(node, *node.left);
                node.setRight(tree.makeNode({ ExprOpType::CONSTANT, exp_first * exp_second }));
            }
            changed = true;
        }

        // abs(abs(x)) = abs(x)
        if (node.op == ExprOpType::ABS && node.left->op == ExprOpType::ABS) {
            replaceNode(node, *node.left);
            changed = true;
        }

        // 0 ? x : y = y    1 ? x : y = x
        if (node.op == ExprOpType::TERNARY && isConstant(*node.left)) {
            ExpressionTreeNode *replacement = node.left->op.imm.f > 0.0f ? node.right->left : node.right->right;
            replaceNode(node, *replacement);
            changed = true;
        }

        // a <= b ? x : y --> a > b ? y : x    a >= b ? x : y --> a < b ? y : x
        if (node.op == ExprOpType::TERNARY && node.left->op.type == ExprOpType::CMP) {
            ComparisonType type = static_cast<ComparisonType>(node.left->op.imm.u);

            if (type == ComparisonType::LE || type == ComparisonType::NLT) {
                node.left->op.imm.u = static_cast<unsigned>(type == ComparisonType::LE ? ComparisonType::NLE : ComparisonType::LT);
                std::swap(node.right->left, node.right->right);
                changed = true;
            }
        }

        // !a ? b : c --> a ? c : b
        if (node.op == ExprOpType::TERNARY && node.left->op == ExprOpType::NOT) {
            replaceNode(*node.left, *node.left->left);
            std::swap(node.right->left, node.right->right);
            changed = true;
        }

        // !(a < b) --> a >= b
        if (node.op == ExprOpType::NOT && node.left->op.type == ExprOpType::CMP) {
            switch (static_cast<ComparisonType>(node.left->op.imm.u)) {
            case ComparisonType::EQ: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::NEQ); break;
            case ComparisonType::LT: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::NLT); break;
            case ComparisonType::LE: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::NLE); break;
            case ComparisonType::NEQ: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::EQ); break;
            case ComparisonType::NLT: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::LT); break;
            case ComparisonType::NLE: node.left->op.imm.u = static_cast<unsigned>(ComparisonType::LE); break;
            }
            replaceNode(node, *node.left);
            changed = true;
        }
    });

    return changed;
}

bool applyStrengthReduction(ExpressionTree &tree)
{
    bool changed = false;

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op == ExprOpType::MUX)
            return;

        // 0 - x = -x
        if (node.op == ExprOpType::SUB && isConstant(*node.left, 0.0f)) {
            ExpressionTreeNode *tmp = node.right;
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::NEG } });
            node.setLeft(tmp);
            changed = true;
        }

        // x * -1 = -x    x / -1 = -x
        if (isOpCode(node, { ExprOpType::MUL, ExprOpType::DIV }) && isConstant(*node.right, -1.0f)) {
            ExpressionTreeNode *tmp = node.left;
            replaceNode(node, ExpressionTreeNode{ { ExprOpType::NEG } });
            node.setLeft(tmp);
            changed = true;
        }

        // a + -b = a - b    a - -b = a + b
        if (isOpCode(node, { ExprOpType::ADD, ExprOpType::SUB }) && node.right->op.type == ExprOpType::NEG) {
            node.op = node.op == ExprOpType::ADD ? ExprOpType::SUB : ExprOpType::ADD;
            replaceNode(*node.right, *node.right->left);
            changed = true;
        }

        // -a + b = b - a
        if (node.op == ExprOpType::ADD && node.left->op == ExprOpType::NEG) {
            node.op = ExprOpType::SUB;
            replaceNode(*node.left, *node.left->left);
            std::swap(node.left, node.right);
        }

        // -(a - b) = b - a
        if (node.op == ExprOpType::NEG && node.left->op == ExprOpType::SUB) {
            replaceNode(node, *node.left);
            std::swap(node.left, node.right);
            changed = true;
        }

        // x * 2 = x + x
        if (node.op == ExprOpType::MUL && isConstant(*node.right, 2.0f) &&
            (!node.parent || !isOpCode(*node.parent, { ExprOpType::ADD, ExprOpType::SUB })))
        {
            ExpressionTreeNode *replacement = tree.clone(node.left);
            node.op = ExprOpType::ADD;
            replaceNode(*node.right, *replacement);
            changed = true;
        }

        // x / y = x * (1 / y)
        if (node.op == ExprOpType::DIV && isConstant(*node.right)) {
            node.op = ExprOpType::MUL;
            node.right->op.imm.f = 1.0f / node.right->op.imm.f;
            changed = true;
        }

        // (1 / x) * y = y / x
        if (node.op == ExprOpType::MUL && node.left->op == ExprOpType::DIV && isConstant(*node.left->left, 1.0f)) {
            node.op = ExprOpType::DIV;
            replaceNode(*node.left, *node.left->right);
            std::swap(node.left, node.right);
            changed = true;
        }

        // x * (1 / y) = x / y
        if (node.op == ExprOpType::MUL && node.right->op == ExprOpType::DIV && isConstant(*node.right->left, 1.0f)) {
            node.op = ExprOpType::DIV;
            replaceNode(*node.right, *node.right->right);
            changed = true;
        }

        // (a / b) * c = (a * c) / b
        if (node.op == ExprOpType::MUL && node.left->op == ExprOpType::DIV) {
            node.op = ExprOpType::DIV;
            node.left->op = ExprOpType::MUL;
            swapNodeContents(*node.left->right, *node.right);
            changed = true;
        }

        // a * (b / c) = (a * b) / c
        if (node.op == ExprOpType::MUL && node.right->op == ExprOpType::DIV) {
            node.op = ExprOpType::DIV;
            node.right->op = ExprOpType::MUL;
            std::swap(node.left, node.right); // (b * c) / a
            swapNodeContents(*node.left->left, *node.left->right); // (c * b) / a
            swapNodeContents(*node.left->left, *node.right); // (a * b) / c
            changed = true;
        }

        // a / (b / c) = (a * c) / b
        if (node.op == ExprOpType::DIV && node.right->op == ExprOpType::DIV) {
            node.right->op = ExprOpType::MUL; // a / (b * c)
            std::swap(node.left, node.right); // (b * c) / a
            swapNodeContents(*node.left->left, *node.right); // (a * c) / b
            changed = true;
        }

        // (a / b) / c = a / (b * c)
        if (node.op == ExprOpType::DIV && node.left->op == ExprOpType::DIV) {
            node.left->op = ExprOpType::MUL; // (a * b) / c
            std::swap(node.left, node.right); // c / (a * b)
            swapNodeContents(*node.left, *node.right->left); // a / (c * b)
            swapNodeContents(*node.right->left, *node.right->right); // a / (b * c)
            changed = true;
        }

        // x ** (n / 2) = sqrt(x ** n)    x ** (n / 4) = sqrt(sqrt(x ** n))
        if (node.op == ExprOpType::POW && isConstant(*node.right) && !isInteger(node.right->op.imm.f) && isInteger(node.right->op.imm.f * 4.0f)) {
            ExpressionTreeNode *dup = tree.clone(&node);
            replaceNode(node, ExpressionTreeNode{ ExprOpType::SQRT });
            node.setLeft(dup);
            node.left->right->op.imm.f *= 2.0f;
            changed = true;
        }

        // x ** -N = 1 / (x ** N)
        if (node.op == ExprOpType::POW && isConstant(*node.right) && isInteger(node.right->op.imm.f) && node.right->op.imm.f < 0) {
            ExpressionTreeNode *dup = tree.clone(&node);
            replaceNode(node, ExpressionTreeNode{ ExprOpType::DIV });
            node.setLeft(tree.makeNode({ ExprOpType::CONSTANT, 1.0f }));
            node.setRight(dup);
            node.right->right->op.imm.f = -node.right->right->op.imm.f;
            changed = true;
        }

        // x ** N = x * x * x * ...
        //
        // This step is required, or else the canonical expressions generated by the algebraic pass will evaluate incorrectly
        // when processed by the inexact pow() functions used in JIT, e.g. negative bases are unsupported!
        if (node.op == ExprOpType::POW && isConstant(*node.right) && isInteger(node.right->op.imm.f) && node.right->op.imm.f > 0) {
            ExpressionTreeNode *replacement = emitIntegerPow(tree, *node.left, static_cast<int>(node.right->op.imm.f));
            replaceNode(node, *replacement);
            changed = true;
        }
    });

    return changed;
}

bool applyOpFusion(ExpressionTree &tree)
{
    std::unordered_map<int, size_t> refCount;
    bool changed = false;

    applyValueNumbering(tree);

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op == ExprOpType::MUX)
            return;

        refCount[node.valueNum]++;
    });

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op == ExprOpType::MUX)
            return;

        auto canElide = [&](ExpressionTreeNode &candidate)
        {
            return refCount[node.valueNum] > 1 || refCount[candidate.valueNum] <= 1;
        };

        // a + (b * c)    (b * c) + a    a - (b * c)    (b * c) - a
        if (node.op == ExprOpType::ADD && node.right->op == ExprOpType::MUL && canElide(*node.right)) {
            node.right->op = ExprOpType::MUX;
            node.op = { ExprOpType::FMA, static_cast<unsigned>(FMAType::FMADD) };
            changed = true;
        }
        if (node.op == ExprOpType::ADD && node.left->op == ExprOpType::MUL && canElide(*node.left)) {
            std::swap(node.left, node.right);
            node.right->op = ExprOpType::MUX;
            node.op = { ExprOpType::FMA, static_cast<unsigned>(FMAType::FMADD) };
            changed = true;
        }
        if (node.op == ExprOpType::SUB && node.right->op == ExprOpType::MUL && canElide(*node.right)) {
            node.right->op = ExprOpType::MUX;
            node.op = { ExprOpType::FMA, static_cast<unsigned>(FMAType::FNMADD) };
            changed = true;
        }
        if (node.op == ExprOpType::SUB && node.left->op == ExprOpType::MUL && canElide(*node.left)) {
            std::swap(node.left, node.right);
            node.right->op = ExprOpType::MUX;
            node.op = { ExprOpType::FMA, static_cast<unsigned>(FMAType::FMSUB) };
            changed = true;
        }

        // (a + b) * c = (a * c) + b * c
        if (node.op == ExprOpType::MUL && isOpCode(*node.left, { ExprOpType::ADD, ExprOpType::SUB }) &&
            isConstant(*node.right) && isConstant(*node.left->right) && canElide(*node.left))
        {
            std::swap(node.op, node.left->op);
            swapNodeContents(*node.right, *node.left->right);
            node.right->op.imm.f *= node.left->right->op.imm.f;
            changed = true;
        }

        // Negative FMA.
        if (node.op == ExprOpType::NEG && node.left->op == ExprOpType::FMA && canElide(*node.left)) {
            replaceNode(node, *node.left);

            switch (static_cast<FMAType>(node.op.imm.u)) {
            case FMAType::FMADD: node.op.imm.u = static_cast<unsigned>(FMAType::FNMSUB); break;
            case FMAType::FMSUB: node.op.imm.u = static_cast<unsigned>(FMAType::FNMADD); break;
            case FMAType::FNMADD: node.op.imm.u = static_cast<unsigned>(FMAType::FMSUB); break;
            case FMAType::FNMSUB: node.op.imm.u = static_cast<unsigned>(FMAType::FMADD); break;
            }

            changed = true;
        }
    });

    return changed;
}

void renameRegisters(std::vector<ExprInstruction> &code)
{
    std::unordered_map<int, int> table;
    std::set<int> freeList;

    for (size_t i = 0; i < code.size(); ++i) {
        ExprInstruction &insn = code[i];
        int origRegs[4] = { insn.dst, insn.src1, insn.src2, insn.src3 };
        int renamed[4] = { insn.dst, insn.src1, insn.src2, insn.src3 };

        for (int n = 1; n < 4; ++n) {
            if (origRegs[n] < 0)
                continue;

            auto it = table.find(origRegs[n]);
            if (it != table.end())
                renamed[n] = it->second;

            bool dead = true;

            for (size_t j = i + 1; j < code.size(); ++j) {
                const ExprInstruction &insn2 = code[j];
                if (insn2.src1 == origRegs[n] || insn2.src2 == origRegs[n] || insn2.src3 == origRegs[n]) {
                    dead = false;
                    break;
                }
            }

            if (dead)
                freeList.insert(renamed[n]);
        }

        if (origRegs[0] >= 0 && !freeList.empty()) {
            renamed[0] = *freeList.begin();
            table[origRegs[0]] = renamed[0];
            freeList.erase(freeList.begin());
            freeList.insert(origRegs[0]);
        }

        insn.dst = renamed[0];
        insn.src1 = renamed[1];
        insn.src2 = renamed[2];
        insn.src3 = renamed[3];
    }
}

std::vector<ExprInstruction> compile(ExpressionTree &tree, const VSVideoInfo &vi, bool optimize = true)
{
    std::vector<ExprInstruction> code;
    std::unordered_set<int> found;

    if (!tree.getRoot())
        return code;

    if (optimize) {
        constexpr unsigned max_passes = 1000;
        unsigned num_passes = 0;

        while (applyLocalOptimizations(tree) || combinePowerTerms(tree) || applyAlgebraicOptimizations(tree) || applyComparisonOptimizations(tree)) {
            if (++num_passes > max_passes)
                throw std::runtime_error{ "expression compilation did not complete" };
        }

        while (applyLocalOptimizations(tree) || applyStrengthReduction(tree) || applyOpFusion(tree)) {
            if (++num_passes > max_passes)
                throw std::runtime_error{ "expression compilation did not complete" };
        }
    }

    applyValueNumbering(tree);

    tree.getRoot()->postorder([&](ExpressionTreeNode &node)
    {
        if (node.op.type == ExprOpType::MUX)
            return;
        if (found.find(node.valueNum) != found.end())
            return;

        ExprInstruction opcode(node.op);
        opcode.dst = node.valueNum;

        if (node.left) {
            assert(node.left->valueNum >= 0);
            opcode.src1 = node.left->valueNum;
        }
        if (node.right) {
            if (node.right->op.type == ExprOpType::MUX) {
                assert(node.right->left->valueNum >= 0);
                assert(node.right->right->valueNum >= 0);
                opcode.src2 = node.right->left->valueNum;
                opcode.src3 = node.right->right->valueNum;
            } else {
                assert(node.right->valueNum >= 0);
                opcode.src2 = node.right->valueNum;
            }
        }

        code.push_back(opcode);
        found.insert(node.valueNum);
    });

    ExprInstruction store(ExprOpType::MEM_STORE_U8);
    const VSVideoFormat &format = vi.format;

    if (format.sampleType == stInteger && format.bytesPerSample == 1)
        store.op.type = ExprOpType::MEM_STORE_U8;
    else if (format.sampleType == stInteger && format.bytesPerSample == 2)
        store.op.type = ExprOpType::MEM_STORE_U16;
    else if (format.sampleType == stFloat && format.bytesPerSample == 2)
        store.op.type = ExprOpType::MEM_STORE_F16;
    else if (format.sampleType == stFloat && format.bytesPerSample == 4)
        store.op.type = ExprOpType::MEM_STORE_F32;

    if (store.op.type == ExprOpType::MEM_STORE_U16)
        store.op.imm.u = format.bitsPerSample;

    store.src1 = code.back().dst;
    code.push_back(store);

    renameRegisters(code);
    return code;
}

} // namespace


std::vector<ExprInstruction> compile(const std::string &expr, const VSVideoInfo * const srcFormats[], int numInputs, const VSVideoInfo &dstFormat, bool optimize)
{
    ExpressionTree tree = parseExpr(expr, srcFormats, numInputs);
    return compile(tree, dstFormat, optimize);
}

} // namespace expr
