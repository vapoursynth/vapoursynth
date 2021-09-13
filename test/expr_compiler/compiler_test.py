import os
import subprocess

EXPR_DEBUGGER_PATH = os.path.realpath(os.path.dirname(__file__) + \
                     "../../../msvc_project/x64/Debug/ExprDebugger.exe")

TEST_CASES = ['havs_exprs.txt', 'muvs_exprs.txt']
OUTPUT = 'compiler_output.txt'

def main():
    with open(OUTPUT, 'wb') as o:
        for test_file in TEST_CASES:
            where = os.path.realpath(os.path.dirname(__file__) + "/" +
                                     test_file)
            with open(where, 'r') as file:
                for line in file:
                    result = subprocess.check_output([EXPR_DEBUGGER_PATH, line],
                                                     creationflags=0x08000000)
                    o.write(result)

if __name__ == '__main__':
    main()
