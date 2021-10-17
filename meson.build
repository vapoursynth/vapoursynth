project('vapoursynth', ['c', 'cpp'],
    default_options: ['buildtype=release', 'default_library=both', 'warning_level=2', 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++14'],
    license: 'LGPL-2.1-or-later',
    meson_version: '>=0.53.0',
    version: '57'
)

enable_guard_pattern = get_option('enable_guard_pattern')
enable_x86_asm = get_option('enable_x86_asm')
enable_core = get_option('enable_core')
enable_vsscript = get_option('enable_vsscript')
enable_vspipe = get_option('enable_vspipe')
enable_python_module = get_option('enable_python_module')
plugindir = get_option('plugindir')
python3_bin = get_option('python3_bin')
cython3_bin = get_option('cython3_bin')

cxx = meson.get_compiler('cpp')

host_cpu_family = host_machine.cpu_family()
host_system = host_machine.system()

incdir = include_directories('include')

lang = ['c', 'cpp']

pkg = import('pkgconfig')

if plugindir == '$libdir/vapoursynth'
    plugindir = get_option('prefix') / get_option('libdir') / 'vapoursynth'
endif

py_module_linking = false

vspipe_link_args = []

add_project_arguments(['-Wno-ignored-attributes', '-Wno-unused-function', '-Wno-unused-parameter'], language: lang)

if host_cpu_family in ['arm', 'aarch64']
    add_project_arguments('-DVS_TARGET_CPU_ARM', language: lang)
elif host_cpu_family.startswith('ppc')
    add_project_arguments('-DVS_TARGET_CPU_POWERPC', language: lang)
elif host_cpu_family.startswith('x86')
    if enable_x86_asm
        add_project_arguments('-DVS_TARGET_CPU_X86', language: lang)
    endif
    add_project_arguments(['-mfpmath=sse', '-msse2'], language: lang)
endif

if not host_cpu_family.startswith('x86')
    enable_x86_asm = false
endif

if host_system == 'darwin'
    add_project_arguments('-DVS_TARGET_OS_DARWIN', language: lang)
elif host_system in ['dragonfly', 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd']
    add_project_arguments('-DVS_TARGET_OS_LINUX', language: lang)
elif host_system in ['cygwin', 'windows']
    add_project_arguments(['-DVS_TARGET_OS_WINDOWS', '-DUNICODE', '-D_UNICODE'], language: lang)
    if host_cpu_family == 'x86'
        add_project_arguments('-mstackrealign', language: lang)
    endif
    py_module_linking = true
    vspipe_link_args = '-municode'
else
    warning('Unknown host OS')
endif

if enable_guard_pattern
    add_project_arguments('-DVS_FRAME_GUARD', language: lang)
endif

if enable_vsscript or enable_python_module
    py = import('python').find_installation(python3_bin)
    py_dep = py.dependency(embed: true, required: false)
    if not py_dep.found()
        py_dep = py.dependency(embed: false)
    endif
endif

if enable_core
    compile_args = ['-DVS_CORE_EXPORTS', '-DVS_GRAPH_API', '-DVS_PATH_PLUGINDIR="@0@"'.format(plugindir)]

    deps = [
        dependency('threads'),
        dependency('zimg', version: '>=3.0.0')
    ]

    if host_system not in ['cygwin', 'windows']
        libdl = cxx.find_library('dl', required: false)
        if not libdl.found()
            libdl = cxx.find_library('dld')
        endif

        deps += libdl

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
            compile_args += '-DHAVE_SCHED_GETAFFINITY'
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
            compile_args += '-DHAVE_CPUSET_GETAFFINITY'
        endif
    endif

    sources = [
        'src/core/expr/expr.cpp',
        'src/core/expr/expr.h',
        'src/core/expr/jitcompiler.cpp',
        'src/core/expr/jitcompiler.h',
        'src/core/kernel/average.cpp',
        'src/core/kernel/average.h',
        'src/core/kernel/cpulevel.cpp',
        'src/core/kernel/cpulevel.h',
        'src/core/kernel/generic.cpp',
        'src/core/kernel/generic.h',
        'src/core/kernel/merge.c',
        'src/core/kernel/merge.h',
        'src/core/kernel/planestats.c',
        'src/core/kernel/planestats.h',
        'src/core/kernel/transpose.c',
        'src/core/kernel/transpose.h',
        'src/core/audiofilters.cpp',
        'src/core/averageframesfilter.cpp',
        'src/core/boxblurfilter.cpp',
        'src/core/cpufeatures.cpp',
        'src/core/cpufeatures.h',
        'src/core/exprfilter.cpp',
        'src/core/filtershared.h',
        'src/core/genericfilters.cpp',
        'src/core/internalfilters.h',
        'src/core/intrusive_ptr.h',
        'src/core/lutfilters.cpp',
        'src/core/memoryuse.cpp',
        'src/core/memoryuse.h',
        'src/core/mergefilters.cpp',
        'src/core/reorderfilters.cpp',
        'src/core/settings.cpp',
        'src/core/settings.h',
        'src/core/simplefilters.cpp',
        'src/core/ter-116n.h',
        'src/core/textfilter.cpp',
        'src/core/VapourSynth3.h',
        'src/core/version.h',
        'src/core/vsapi.cpp',
        'src/core/vscore.cpp',
        'src/core/vscore.h',
        'src/core/vslog.cpp',
        'src/core/vslog.h',
        'src/core/vsresize.cpp',
        'src/core/vsthreadpool.cpp',
        'src/core/x86utils.h'
    ]

    avx2 = []

    if enable_x86_asm
        sources += [
            'src/core/expr/jitasm.h',
            'src/core/expr/jitcompiler_x86.cpp',
            'src/core/kernel/x86/average_sse2.c',
            'src/core/kernel/x86/convolution_sse2.cpp',
            'src/core/kernel/x86/generic_sse2.cpp',
            'src/core/kernel/x86/merge_sse2.c',
            'src/core/kernel/x86/planestats_sse2.c',
            'src/core/kernel/x86/transpose_sse2.c'
        ]

        avx2_sources = [
            'src/core/kernel/x86/convolution_avx2.cpp',
            'src/core/kernel/x86/generic_avx2.cpp',
            'src/core/kernel/x86/merge_avx2.c',
            'src/core/kernel/x86/planestats_avx2.c'
        ]

        avx2 = static_library('avx2', avx2_sources,
            c_args: compile_args + ['-mavx2', '-mfma'],
            cpp_args: compile_args + ['-mavx2', '-mfma'],
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir
        )
    endif # enable_x86_asm

    libvapoursynth = library('vapoursynth', sources,
        c_args: compile_args,
        cpp_args: compile_args,
        dependencies: deps,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
        install: true,
        link_with: avx2
    )

    headers = [
        'include/VapourSynth.h',
        'include/VapourSynth4.h',
        'include/VSConstants4.h',
        'include/VSHelper.h',
        'include/VSHelper4.h',
        'include/VSScript.h',
        'include/VSScript4.h'
    ]

    install_headers(headers,
        subdir: 'vapoursynth'
    )

    pkg.generate(libvapoursynth,
        description: 'A frameserver for the 21st century',
        filebase: 'vapoursynth',
        subdirs: 'vapoursynth'
    )

    if enable_python_module
        cython = find_program(cython3_bin)

        vapoursynth_pyx_c = custom_target('vapoursynth_pyx',
            command: [cython, '-o', '@OUTPUT@', '@INPUT@'],
            input: 'src/cython/vapoursynth.pyx',
            output: 'vapoursynth_pyx.c'
        )

        py.extension_module('vapoursynth', vapoursynth_pyx_c,
            dependencies: py_dep.partial_dependency(compile_args: true, includes: true, link_args: py_module_linking, links: py_module_linking),
            install: true,
            install_dir: py.get_install_dir(),
            link_with: libvapoursynth
        )
    endif # enable_python_module
elif enable_python_module
    warning('Cannot build the Python module when the core is disabled.')
    enable_python_module = false
endif # enable_core

if enable_vsscript
    sources = [
        'src/vsscript/vsscript.cpp',
        'src/vsscript/vsscript_internal.h'
    ]

    libvsscript = library('vapoursynth-script', sources,
        cpp_args: '-DVS_CORE_EXPORTS',
        dependencies: py_dep,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
        install: true,
        soversion: 0
    )

    pkg.generate(libvsscript,
        description: 'Library for interfacing VapourSynth with Python',
        filebase: 'vapoursynth-script',
        subdirs: 'vapoursynth'
    )

    if enable_vspipe
        sources = [
            'src/common/wave.cpp',
            'src/common/wave.h',
            'src/vspipe/md5.c',
            'src/vspipe/md5.h',
            'src/vspipe/printgraph.cpp',
            'src/vspipe/printgraph.h',
            'src/vspipe/vspipe.cpp'
        ]

        executable('vspipe', sources,
            cpp_args: '-DVS_GRAPH_API',
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
            install: true,
            link_args: vspipe_link_args,
            link_with: libvsscript
        )
    endif # enable_vspipe
elif enable_vspipe
    warning('Cannot build vspipe when VSScript is disabled.')
    enable_vspipe = false
endif # enable_vsscript

summary({
    'prefix': get_option('prefix'),
    'bindir': get_option('bindir'),
    'includedir': get_option('includedir'),
    'libdir': get_option('libdir')
}, section: 'Directories')

summary({
    'buildtype': get_option('buildtype'),
    'debug': get_option('debug'),
    'default_library': get_option('default_library'),
    'optimization': get_option('optimization'),
    'warning_level': get_option('warning_level')
}, section: 'Core options')

summary({
    'enable_guard_pattern': enable_guard_pattern,
    'enable_x86_asm': enable_x86_asm,
    'enable_core': enable_core,
    'enable_vsscript': enable_vsscript,
    'enable_vspipe': enable_vspipe,
    'enable_python_module': enable_python_module,
    'plugindir': plugindir,
    'python3_bin': python3_bin,
    'cython3_bin': cython3_bin
}, section: 'Build options')
