from waftools.inflectors import DependencyInflector
from waftools.checks.generic import *
from waflib import Utils
import os

__all__ = ["check_python", "check_cpu_x86", "check_cpu_x86_64"]

def check_python(ctx, dependency_identifier):
    ctx.find_program(['python3', 'python'], var = 'PYTHON')
    ctx.load('python')

    ctx.check_python_version()

    ver = int(ctx.env.PYTHON_VERSION.split('.')[0])
    if (ver == 3):
        ctx.check_python_headers()
        return True

    return False

def check_cpu_x86(ctx, dependency_identifier):
    return ctx.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']

def check_cpu_x86_64(ctx, dependency_identifier):
    return ctx.env.DEST_CPU in ['x86_64', 'x64', 'amd64', 'x86_amd64']
