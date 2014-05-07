from waflib import Utils

def __add_options__(ctx, flags, options):
    for flag in flags:
        ctx.env.append_unique(flag, options)

def __get_cc_env_vars__(ctx):
    if ctx.env.CC_NAME == 'msvc':
        return '__msvc__'
    else:
        cmd = ctx.env.CC + ['-dM', '-E', '-']
        try:
            p = Utils.subprocess.Popen(cmd, stdin=Utils.subprocess.PIPE,
                                            stdout=Utils.subprocess.PIPE,
                                            stderr=Utils.subprocess.PIPE)
            p.stdin.write('\n'.encode())
            return p.communicate()[0]
        except Exception:
            return ''

def __add_gcc_flags__(ctx):
    __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                    ['-Wall', '-Wundef',
                     #'-Wshadow',
                     '-Wno-switch', '-Wno-unused-function',
                     '-Wno-parentheses', '-Wpointer-arith', '-Wredundant-decls',
                     '-Werror=implicit-function-declaration',
                     '-Wno-error=deprecated-declarations'])

    __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                    ['-DVS_CORE_EXPORTS', '-fPIC', '-msse2'])

    ctx.env.CXXFLAGS += ['-std=c++0x']
    ctx.env.CFLAGS += ['-std=c99', '-Wmissing-prototypes', '-Wno-pointer-sign']

    if ctx.is_debug_build():
        __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                        ['-DVS_CORE_DEBUG', '-g3', '-ggdb', '-ftrapv'])
    else:
        __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'], ['-O3'])

def __add_clang_flags__(ctx):
    __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                    ['-Wno-logical-op-parentheses', '-fcolor-diagnostics'])

def __add_msvc_flags__(ctx):
    __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                    ['/DVS_CORE_EXPORTS', '/EHsc', '/Zc:wchar_t-'])

    if ctx.is_debug_build():
        __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'],
                        ['/DVS_CORE_DEBUG', '/Z7'])
    else:
        __add_options__(ctx, ['CFLAGS', 'CXXFLAGS'], ['/Ox'])

__compiler_map__ = {
    '__GNUC__':  __add_gcc_flags__,
    '__clang__': __add_clang_flags__,
    '__msvc__': __add_msvc_flags__
}

def __apply_map__(ctx, fnmap):
    if not getattr(ctx, 'CC_ENV_VARS', None):
        ctx.CC_ENV_VARS = str(__get_cc_env_vars__(ctx))
    for k, fn in fnmap.items():
        if ctx.CC_ENV_VARS.find(k) != -1:
            fn(ctx)

def __add_as_flags__(ctx):
    if ctx.dependency_satisfied('cpu-x86'):
        ctx.find_program('yasm', var='AS')
        ctx.load('nasm')

        ctx.env.ASFLAGS += ['-w',
                            '-Worphan-labels',
                            '-Wunrecognized-char',
                            '-Dprivate_prefix=vs']

        if ctx.dependency_satisfied('os-darwin') and ctx.env.CC_NAME == 'gcc':
            ctx.env.ASFLAGS += ['-DPREFIX=1']
            fmt = 'macho'
        elif ctx.dependency_satisfied('os-windows'):
            fmt = 'win'
        else:
            fmt = 'elf'

        if ctx.dependency_satisfied('cpu-x86_64'):
            ctx.env.ASFLAGS += ['-DARCH_X86_64=1', '-DPIC=1']
            fmt += '64'
        else:
            ctx.env.ASFLAGS += ['-DARCH_X86_64=0']
            fmt += '32'

        ctx.env.ASFLAGS += ['-f' + fmt]

        if ctx.is_debug_build():
            if ctx.dependency_satisfied('os-windows'):
                debugfmt = 'cv8'
            elif ctx.dependency_satisfied('os-darwin'):
                debugfmt = 'null'
            else:
                debugfmt = 'dwarf2'

            ctx.env.ASFLAGS += ['-g' + debugfmt]
    else:
        ctx.load('gas')

        if ctx.is_debug_build():
            ctx.env.ASFLAGS += ['-Wa,-g']

def configure(ctx):
    __add_as_flags__(ctx)
    __apply_map__(ctx, __compiler_map__)
