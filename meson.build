project('VapourSynth', 'c', 'cpp', 'cython',
    default_options: ['buildtype=release', 'b_lto=true', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++20'],
    license: 'LGPL-2.1-or-later',
    license_files: 'COPYING.LESSER',
    meson_version: '>=1.3.0',
    version: '78rc5',
)

enable_guard_pattern = get_option('enable_guard_pattern')
enable_x86_asm = get_option('enable_x86_asm')
enable_arm_asm = get_option('enable_arm_asm')

cxx = meson.get_compiler('cpp')

if cxx.get_argument_syntax() == 'gcc'
    avx2_args = '-march=x86-64-v3'
    avx512_args = '-march=x86-64-v4'
    znver4_args = ['-march=znver4', '-mno-sse4a', '-mno-avx512bf16']
    math_errno_args = ['-fno-math-errno']
elif cxx.get_id() == 'clang-cl'
    avx2_args = '/clang:-march=x86-64-v3'
    avx512_args = '/clang:-march=x86-64-v4'
    znver4_args = ['/clang:-march=znver4', '/clang:-mno-sse4a', '/clang:-mno-avx512bf16']
    math_errno_args = ['/clang:-fno-math-errno']
else
    avx2_args = '/arch:AVX2'
    avx512_args = '/arch:AVX512'
    znver4_args = ['/arch:AVX512']  # MSVC has no znver4 target; fall back to AVX-512
    math_errno_args = []
endif

# Applied project-wide: lets the compiler lower math calls (e.g. averageframesfilter's
# lrintf) to vector converts instead of libcalls. Safe -- no VapourSynth code reads
# math errno (the only errno reads are fwrite error handling in vspipe, which this
# does not affect).
if math_errno_args.length() > 0
    add_project_arguments(math_errno_args, language: ['c', 'cpp'])
endif

# Disable C++20 char8_t project-wide. The code predates char8_t and treats
# std::filesystem u8string()/u8path() as std::string throughout; keeping char8_t off
# preserves that behaviour under C++20 without any source changes.
if cxx.get_argument_syntax() == 'gcc'
    add_project_arguments('-fno-char8_t', language: 'cpp')
else
    add_project_arguments('/Zc:char8_t-', language: 'cpp')
endif

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()
is_win = host_system in ['cygwin', 'windows']

if is_win
    dl_dep = dependency('', required: false)
    libp2p_dep = dependency('libp2p')
else
    dl_dep = dependency('dl')
endif

if host_cpu_family.startswith('riscv')
    add_project_arguments('-DVS_TARGET_CPU_RISCV', language: ['c', 'cpp'])
endif

if not host_cpu_family.startswith('x86')
    enable_x86_asm = false
endif

if host_cpu_family != 'aarch64'
    enable_arm_asm = false
endif

incdir = include_directories('include')

py = import('python').find_installation(pure: false)
py_dep = py.dependency(version: is_win ? '>=3.14' : '>=3.12')  # Py_NO_LINK_LIB was introduced in 3.14

install_dir = py.get_install_dir() / 'vapoursynth'

vs_current_release = run_command(
    py,
    '-c',
    'print(open("VAPOURSYNTH_VERSION").read())',
    check: true,
).stdout().strip().split()[2].split('-')[0]

if get_option('buildtype') == 'release'
    if cxx.get_id() == 'clang-cl'
        add_project_arguments('/clang:-flto', '/clang:-O3', language: ['c', 'cpp'])
    endif
endif

add_project_arguments(
    cxx.get_supported_arguments(
        '-D_CRT_SECURE_NO_WARNINGS',
        '-DNOMINMAX',
        '-DVS_CURRENT_RELEASE=@0@'.format(vs_current_release),
        '-DVS_GRAPH_API',
        '-DVS_USE_LATEST_API',
        '-DVSSCRIPT_USE_LATEST_API',
        '-Wno-ignored-attributes',
    ),
    language: ['c', 'cpp'],
)

if is_win
    add_project_arguments('-DVS_TARGET_OS_WINDOWS', '-D_UNICODE', '-DUNICODE', language: ['c', 'cpp'])
elif host_system == 'darwin'
    add_project_arguments('-DVS_TARGET_OS_DARWIN', language: ['c', 'cpp'])
elif host_system in ['dragonfly', 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd']
    add_project_arguments('-DVS_TARGET_OS_LINUX', language: ['c', 'cpp'])
else
    warning('Unknown host OS')
endif

if enable_guard_pattern
    add_project_arguments('-DVS_FRAME_GUARD', language: ['c', 'cpp'])
endif

if host_system not in ['cygwin', 'windows']
    code = '''
#define _GNU_SOURCE
#include <sched.h>

int main() {
    cpu_set_t affinity;
    sched_getaffinity(0, sizeof(cpu_set_t), &affinity);
    int count = CPU_COUNT(&affinity);
}
'''

    if cxx.links(code, name: 'sched_getaffinity')
        add_project_arguments('-DHAVE_SCHED_GETAFFINITY', language: ['c', 'cpp'])
    endif

    code = '''
#include <sys/param.h>
#include <sys/_cpuset.h>
#include <sys/cpuset.h>

int main() {
    cpuset_t affinity;
    cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), &affinity);
    int count = CPU_COUNT(&affinity);
}
'''

    if cxx.links(code, name: 'cpuset_getaffinity')
        add_project_arguments('-DHAVE_CPUSET_GETAFFINITY', language: ['c', 'cpp'])
    endif
endif

deps = [
    dl_dep,
    dependency('threads'),
    dependency('zimg', version: '>=3.0.5'),
]

soversion = is_win ? '' : run_command(
    'grep',
    '-m',
    '1',
    'VAPOURSYNTH_API_MAJOR',
    'include/VapourSynth4.h',
    check: true,
).stdout().strip().split()[2]

if enable_x86_asm
    add_project_arguments('-DVS_TARGET_CPU_X86', language: ['c', 'cpp'])
endif

# --- AArch64 SIMD kernels ----------------------------------------------------
# NEON is the AArch64 baseline and its kernels ride along with the plain filter
# sources. fp16fml/i8mm kernels get their own TUs with a raised -march; they are
# runtime-dispatched.
arm_have_i8mm = false
arm_have_fhm = false
if enable_arm_asm
    add_project_arguments('-DVS_TARGET_CPU_ARM64', language: ['c', 'cpp'])

    arm_i8mm_args = ['-march=armv8.2-a+i8mm']
    arm_fhm_args = ['-march=armv8.2-a+fp16+fp16fml']

    arm_have_i8mm = cxx.compiles('''
#include <arm_neon.h>
int32x4_t f(int32x4_t a, uint8x16_t b, int8x16_t c) { return vusdotq_s32(a, b, c); }
''',
        args: arm_i8mm_args,
        name: 'AArch64 i8mm usdot intrinsics',
    )

    arm_have_fhm = cxx.compiles('''
#include <arm_neon.h>
float32x4_t f(float32x4_t r, float16x8_t a, float16x8_t b) { return vfmlalq_low_f16(r, a, b); }
''',
        args: arm_fhm_args,
        name: 'AArch64 fp16fml (FEAT_FHM) intrinsics',
    )

    if arm_have_i8mm
        add_project_arguments('-DVS_TARGET_ARM_I8MM', language: ['c', 'cpp'])
    endif
    if arm_have_fhm
        add_project_arguments('-DVS_TARGET_ARM_FHM', language: ['c', 'cpp'])
    endif
endif

libvapoursynth = library('libvapoursynth',
    files(
        'src/core/cpufeatures.cpp',
        'src/core/internalfilters.cpp',
        'src/core/kernel/cpulevel.cpp',
        'src/core/memoryuse.cpp',
        'src/core/textfilter.cpp',
        'src/core/vsapi.cpp',
        'src/core/vscore.cpp',
        'src/core/vslog.cpp',
        'src/core/vsresize.cpp',
        'src/core/vsthreadpool.cpp',
    ),
    c_args: '-DVS_CORE_EXPORTS',
    cpp_args: '-DVS_CORE_EXPORTS',
    dependencies: deps,
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    install: true,
    install_dir: install_dir,
    name_prefix: '',
    soversion: soversion,
)

vapoursynth_dep = declare_dependency(
    include_directories: incdir,
    link_with: libvapoursynth,
)

py.extension_module('vapoursynth',
    files('src/common/wave.cpp', 'src/cython/vapoursynth.pyx'),
    dependencies: [py_dep, vapoursynth_dep],
    install: true,
    install_rpath: '$ORIGIN',
    limited_api: '3.12',
    override_options: 'cython_language=cpp',
    subdir: 'vapoursynth',
)

py.install_sources(
    files(
        'src/py/__init__.py',
        'src/py/__main__.py',
        'src/py/_cli.py',
        'src/py/_shell.py',
        'src/py/_utils.py',
        'src/py/vapoursynth.pyi',
    ),
    subdir: 'vapoursynth',
)

libvsscript = library('vsscript',
    files('src/vsscript/vsscript.cpp'),
    cpp_args: ['-DPy_NO_LINK_LIB', '-DVS_CORE_EXPORTS'],
    dependencies: [dl_dep, py_dep],
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    install: true,
    install_dir: install_dir,
)

vsscript_dep = declare_dependency(
    include_directories: incdir,
    link_with: libvsscript,
)

link_args = []

if cxx.get_argument_syntax() == 'gcc' and is_win
    link_args += '-municode'
elif cxx.get_argument_syntax() == 'msvc'
    link_args += ['/MANIFEST:EMBED', '/MANIFESTINPUT:' + (meson.project_source_root() / 'src/vspipe/vspipe.manifest')]
endif

executable('vspipe',
    files(
        'src/common/wave.cpp',
        'src/vspipe/printgraph.cpp',
        'src/vspipe/vsjson.cpp',
        'src/vspipe/vspipe.cpp',
    ),
    dependencies: vsscript_dep,
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: install_dir,
    install_rpath: '$ORIGIN',
    link_args: link_args,
)

if cxx.get_argument_syntax() == 'msvc'
    vsvfw = library('vsvfw',
        files(
            'src/common/fourcc.cpp',
            'src/common/wave.cpp',
            'src/vfw/vsvfw.cpp',
        ),
        dependencies: [libp2p_dep, vsscript_dep],
        gnu_symbol_visibility: 'hidden',
        install: true,
        install_dir: install_dir,
        vs_module_defs: 'src/vfw/vsvfw.def',
    )

    avscompat = shared_module('avscompat',
        files(
            'src/avisynth/avisynth_compat.cpp',
            'src/avisynth/interface.cpp',
            'src/core/cpufeatures.cpp',
        ),
        cpp_args: '-DBUILDING_AVSCORE',
        dependencies: libp2p_dep,
        gnu_symbol_visibility: 'hidden',
        include_directories: [incdir, include_directories('src/avisynth')],
        install: true,
        install_dir: install_dir / 'plugins',
    )
endif

# --- libvapoursynthfilters -------------------------------------------------
# The standard filters ship as a standalone plugin. On x86 it is built in several
# whole-program ISA variants: every variant still
# contains the runtime-dispatched AVX2/AVX-512 kernels, but the rest of the plugin is
# compiled for a progressively higher CPU baseline.

# Cross-platform sources (the kernel/*.{c,cpp} dispatchers carry scalar fallbacks).
filters_sources = files(
    'src/core/audiofilters.cpp',
    'src/core/audioresampling.cpp',
    'src/core/averageframesfilter.cpp',
    'src/core/boxblurfilter.cpp',
    'src/core/coreplugins.cpp',
    'src/core/cpufeatures.cpp',
    'src/core/exprfilter.cpp',
    'src/core/expr/expr.cpp',
    'src/core/expr/jitcompiler.cpp',
    'src/core/genericfilters.cpp',
    'src/core/kernel/cpulevel.cpp',
    'src/core/kernel/generic.cpp',
    'src/core/kernel/merge.cpp',
    'src/core/kernel/planestats.cpp',
    'src/core/kernel/transpose.cpp',
    'src/core/lutfilters.cpp',
    'src/core/mergefilters.cpp',
    'src/core/reorderfilters.cpp',
    'src/core/simplefilters.cpp',
)

if enable_x86_asm
    # Per-ISA hand-written kernels. They are runtime-dispatched, so even a baseline
    # build contains them. SSE2 is the x86-64 baseline and rides along with the main
    # sources; the AVX2/AVX-512 kernels go into their own per-variant static libs.
    sse2_kernel_sources = files(
        'src/core/expr/jitcompiler_x86.cpp',
        'src/core/kernel/x86/convolution_sse2.cpp',
        'src/core/kernel/x86/generic_sse2.cpp',
        'src/core/kernel/x86/merge_sse2.cpp',
        'src/core/kernel/x86/planestats_sse2.cpp',
        'src/core/kernel/x86/transpose_sse2.cpp',
    )
    avx2_kernel_sources = files(
        'src/core/kernel/x86/convolution_avx2.cpp',
        'src/core/kernel/x86/generic_avx2.cpp',
        'src/core/kernel/x86/merge_avx2.cpp',
        'src/core/kernel/x86/planestats_avx2.cpp',
    )
    avx512_kernel_sources = files(
        'src/core/kernel/x86/convolution_avx512.cpp',
        'src/core/kernel/x86/generic_avx512.cpp',
        'src/core/kernel/x86/lut_avx512.cpp',
        'src/core/kernel/x86/merge_avx512.cpp',
    )

    # ISA ladder. The effective -march of a translation unit is the HIGHEST of (its
    # own minimum level, the variant's level).
    march_levels = {
        '0': [],             # baseline (generic x86-64, SSE2)
        '1': [avx2_args],    # AVX2    (x86-64-v3)
        '2': [avx512_args],  # AVX-512 (x86-64-v4)
        '3': znver4_args,    # Zen4 whole-program, highest level
    }
    avx2_min_level = 1
    avx512_min_level = 2

    # All variants are always compiled and installed alongside libvapoursynth.
    # They share the com.vapoursynth.std plugin id; the core picks the one matching
    # the running CPU and ignores the rest.
    variants = [
        { 'name': 'libvapoursynthfilters',        'tag': 'baseline', 'level': 0 },
        { 'name': 'libvapoursynthfilters_avx2',   'tag': 'avx2',     'level': 1 },
        { 'name': 'libvapoursynthfilters_zn4',    'tag': 'znver4',   'level': 3 },
    ]

    foreach v : variants
        g = v['level']
        tag = v['tag']

        avx2_level = (avx2_min_level > g) ? avx2_min_level : g
        avx512_level = (avx512_min_level > g) ? avx512_min_level : g

        vlibs = []

        vlibs += static_library('vsfilters_avx2_' + tag,
            avx2_kernel_sources,
            c_args: march_levels[avx2_level.to_string()],
            cpp_args: march_levels[avx2_level.to_string()],
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
        )

        vlibs += static_library('vsfilters_avx512_' + tag,
            avx512_kernel_sources,
            c_args: march_levels[avx512_level.to_string()],
            cpp_args: march_levels[avx512_level.to_string()],
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
        )

        shared_module(v['name'],
            filters_sources + sse2_kernel_sources,
            c_args: march_levels[g.to_string()],
            cpp_args: march_levels[g.to_string()],
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
            link_with: vlibs,
            name_prefix: '',
            install: true,
            install_dir: install_dir,
        )
    endforeach
else
    arm_filter_sources = []
    arm_vlibs = []
    if enable_arm_asm
        # NEON is the AArch64 baseline; its kernels ride with the plugin proper.
        arm_filter_sources += files(
            'src/core/kernel/arm/convolution_neon.cpp',
            'src/core/kernel/arm/lut_neon.cpp',
            'src/core/kernel/arm/square_neon.cpp',
        )
        if arm_have_i8mm
            # usdot byte square conv. Its own TU so the NEON baseline sources are
            # never built with +i8mm.
            arm_vlibs += static_library('vsfilters_i8mm',
                files('src/core/kernel/arm/square_dot_neon.cpp'),
                cpp_args: arm_i8mm_args,
                gnu_symbol_visibility: 'hidden',
                include_directories: incdir,
            )
        endif
        if arm_have_fhm
            # fmlal widening f16 MAC kernels. Separated so NEON baseline
            # sources aren't built with +fp16fml.
            # Kept out of LTO deliberately. Under gcc 13 + -flto, growing this
            # unit changes how the link re-optimises the UNTOUCHED half
            # scanlines in convolution_neon.cpp: adding the 1D kernels below
            # cost half nof16 h25 -10..-13% and hv9 -14..-17% on V1/V2/V3, on
            # a function whose source never changed (verified in the
            # disassembly: same source, different codegen). Compiling to real
            # object code keeps it out of the LTO partition and the regression
            # disappears (nof16 back to +-0%). It costs the fhm kernels
            # themselves almost nothing; the squares measure identical to the
            # LTO build. Only f16 h25 gives up ~3% because they're
            # self-contained with header inlines as helpers.
            arm_vlibs += static_library('vsfilters_fhm',
                files('src/core/kernel/arm/convolution_fhm_neon.cpp'),
                cpp_args: arm_fhm_args,
                gnu_symbol_visibility: 'hidden',
                include_directories: incdir,
                override_options: ['b_lto=false'],
            )
        endif
    endif

    shared_module('libvapoursynthfilters',
        filters_sources + arm_filter_sources,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
        link_with: arm_vlibs,
        name_prefix: '',
        install: true,
        install_dir: install_dir,
    )
endif

install_subdir(
    'include',
    exclude_directories: 'cython',
    exclude_files: ['VapourSynth.h', 'VSHelper.h'],
    install_dir: install_dir,
)

pc_data = configuration_data()
pc_data.set('version', meson.project_version())
configure_file(
    configuration: pc_data,
    input: 'vapoursynth.pc.in',
    install: true,
    install_dir: install_dir / 'pkgconfig',
    output: 'vapoursynth.pc',
)
