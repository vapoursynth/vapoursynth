#include <iomanip>
#include <iostream>
#include "VapourSynth4.h"
#include "expr.h"

using namespace expr;

static const char *op_names[] = {
	"loadu8", "loadu16", "loadf16", "loadf32", "constant",
	"storeu8", "storeu16", "storef16", "storef32",
	"add", "sub", "mul", "div", "fma", "sqrt", "abs", "neg", "max", "min", "cmp",
	"and", "or", "xor", "not",
	"exp", "log", "pow", "sin", "cos",
	"ternary",
	"mux",
	"dup", "swap",
};
static_assert(sizeof(op_names) / sizeof(op_names[0]) == static_cast<size_t>(ExprOpType::SWAP) + 1, "");

static const char *cmp_names[8] = {
	"EQ", "LT", "LE", "?", "NEQ", "NLT", "NLE", "?"
};

int main(int argc, char **argv) try
{
	VSVideoInfo realvi{};
	const VSVideoInfo *vi[26];

	realvi.format.bytesPerSample = 1;
	realvi.format.sampleType = stInteger;

	for (int i = 0; i < 26; ++i) {
		vi[i] = &realvi;
	}

	if (argc >= 2) {
		std::cout << argv[1] << '\n';
		bool optimize = argc > 2 ? !!std::atoi(argv[2]) : true;

		std::vector<ExprInstruction> code = compile(argv[1], vi, 26, realvi, optimize);

		for (auto &insn : code) {
			std::cout << std::setw(12) << std::left << op_names[static_cast<size_t>(insn.op.type)];

			if (insn.op.type == ExprOpType::MEM_STORE_U8 || insn.op.type == ExprOpType::MEM_STORE_U16 || insn.op.type == ExprOpType::MEM_STORE_F16 || insn.op.type == ExprOpType::MEM_STORE_F32) {
				std::cout << " r" << insn.src1 << '\n';
				continue;
			}

			std::cout << " r" << insn.dst;

			if (insn.src1 >= 0)
				std::cout << ",r" << insn.src1;
			if (insn.src2 >= 0)
				std::cout << ",r" << insn.src2;
			if (insn.src3 >= 0)
				std::cout << ",r" << insn.src3;

			switch (insn.op.type) {
			case ExprOpType::MEM_LOAD_U8:
			case ExprOpType::MEM_LOAD_U16:
			case ExprOpType::MEM_LOAD_F16:
			case ExprOpType::MEM_LOAD_F32:
				std::cout << ',' << static_cast<char>(insn.op.imm.u < 3 ? 'x' + insn.op.imm.u : 'a' + insn.op.imm.u - 3);
				break;
			case ExprOpType::CONSTANT:
				std::cout << ',' << insn.op.imm.f;
				break;
			case ExprOpType::FMA:
				std::cout << "," << insn.op.imm.u;
				break;
			case ExprOpType::CMP:
				std::cout << ',' << cmp_names[insn.op.imm.u];
				break;
			}

			std::cout << '\n';
		}
	}

	return 0;
} catch (const std::exception &e) {
	std::cout << e.what() << '\n';
	throw;
}
