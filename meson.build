project('VapourSynth', ['c', 'cpp', 'cython'],
    default_options: ['buildtype=release', 'b_lto=true', 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++17'],
    license: 'LGPL-2.1-or-later',
    meson_version: '>=0.63.0',
    version: run_command('cat', 'VAPOURSYNTH_VERSION', check: true).stdout().strip().split()[2].split('-')[0],
)

enable_guard_pattern = get_option('enable_guard_pattern')
enable_x86_asm = get_option('enable_x86_asm')
enable_vsscript = get_option('enable_vsscript')
enable_vspipe = get_option('enable_vspipe')
enable_python_module = get_option('enable_python_module')
plugindir = get_option('plugindir')
python3_bin = get_option('python3_bin')

cxx = meson.get_compiler('cpp')

deps = [dependency('zimg', version: '>=3.0.5')]

host_cpu_family = host_machine.cpu_family()
host_system = host_machine.system()

if not host_cpu_family.startswith('x86')
    enable_x86_asm = false
endif

incdir = include_directories('include')

lang = ['c', 'cpp']

libs = []

if plugindir == '$libdir/vapoursynth'
    plugindir = get_option('prefix') / get_option('libdir') / 'vapoursynth'
endif

vspipe_link_args = []

add_project_arguments(
    [
        '-DVS_CORE_EXPORTS',
        '-DVS_CURRENT_RELEASE=@0@'.format(meson.project_version()),
        '-DVS_GRAPH_API',
        '-DVS_PATH_PLUGINDIR="@0@"'.format(plugindir),
        '-DVS_USE_LATEST_API',
        '-DVSSCRIPT_USE_LATEST_API',
        '-Wno-ignored-attributes',
    ],
    language: lang,
)

if get_option('buildtype') == 'release'
    add_project_arguments(['-fno-math-errno', '-fno-trapping-math'], language: lang)
endif

if host_system == 'darwin'
    add_project_arguments('-DVS_TARGET_OS_DARWIN', language: lang)
elif host_system in ['dragonfly', 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd']
    add_project_arguments('-DVS_TARGET_OS_LINUX', language: lang)
elif host_system in ['cygwin', 'windows']
    add_project_arguments(['-DVS_TARGET_OS_WINDOWS', '-DUNICODE', '-D_UNICODE'], language: lang)
    vspipe_link_args = '-municode'
else
    warning('Unknown host OS')
endif

if host_system not in ['cygwin', 'windows']
    deps += dependency('dl')

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
        add_project_arguments('-DHAVE_SCHED_GETAFFINITY', language: lang)
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
        add_project_arguments('-DHAVE_CPUSET_GETAFFINITY', language: lang)
    endif
endif

if enable_guard_pattern
    add_project_arguments('-DVS_FRAME_GUARD', language: lang)
endif

if enable_vsscript or enable_python_module
    py = import('python').find_installation(python3_bin)
    py_dep = py.dependency(embed: true, required: false, version: '>=3.8.0')
    if not py_dep.found()
        py_dep = py.dependency(embed: false, version: '>=3.8.0')
    endif
endif

if enable_x86_asm
    add_project_arguments('-DVS_TARGET_CPU_X86', language: lang)

    libs += static_library('sse2',
        files(
            'src/core/expr/jitcompiler_x86.cpp',
            'src/core/kernel/x86/average_sse2.c',
            'src/core/kernel/x86/convolution_sse2.cpp',
            'src/core/kernel/x86/generic_sse2.cpp',
            'src/core/kernel/x86/merge_sse2.c',
            'src/core/kernel/x86/planestats_sse2.c',
            'src/core/kernel/x86/transpose_sse2.c',
        ),
        c_args: '-march=x86-64-v2',
        cpp_args: '-march=x86-64-v2',
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )

    libs += static_library('avx2',
        files(
            'src/core/kernel/x86/convolution_avx2.cpp',
            'src/core/kernel/x86/generic_avx2.cpp',
            'src/core/kernel/x86/merge_avx2.c',
            'src/core/kernel/x86/planestats_avx2.c',
        ),
        c_args: '-march=x86-64-v3',
        cpp_args: '-march=x86-64-v3',
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )
endif

libvapoursynth = library('vapoursynth',
    files(
        'src/core/expr/expr.cpp',
        'src/core/expr/jitcompiler.cpp',
        'src/core/kernel/average.cpp',
        'src/core/kernel/cpulevel.cpp',
        'src/core/kernel/generic.cpp',
        'src/core/kernel/merge.c',
        'src/core/kernel/planestats.c',
        'src/core/kernel/transpose.c',
        'src/core/audiofilters.cpp',
        'src/core/averageframesfilter.cpp',
        'src/core/boxblurfilter.cpp',
        'src/core/cpufeatures.cpp',
        'src/core/exprfilter.cpp',
        'src/core/genericfilters.cpp',
        'src/core/lutfilters.cpp',
        'src/core/memoryuse.cpp',
        'src/core/mergefilters.cpp',
        'src/core/reorderfilters.cpp',
        'src/core/settings.cpp',
        'src/core/simplefilters.cpp',
        'src/core/textfilter.cpp',
        'src/core/vsapi.cpp',
        'src/core/vscore.cpp',
        'src/core/vslog.cpp',
        'src/core/vsresize.cpp',
        'src/core/vsthreadpool.cpp',
    ),
    dependencies: deps,
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    install: true,
    link_with: libs,
    soversion: run_command('grep', '-m', '1', 'VAPOURSYNTH_API_MAJOR', 'include/VapourSynth4.h', check: true).stdout().strip().split()[2],
)

install_headers(
    files(
        'include/VapourSynth.h',
        'include/VapourSynth4.h',
        'include/VSConstants4.h',
        'include/VSHelper.h',
        'include/VSHelper4.h',
        'include/VSScript4.h',
    ),
    subdir: 'vapoursynth',
)

pkg = import('pkgconfig')
pkg.generate(libvapoursynth,
    description: 'A frameserver for the 21st century',
    filebase: 'vapoursynth',
    subdirs: 'vapoursynth',
)

vapoursynth_dep = declare_dependency(
    include_directories: incdir,
    link_with: libvapoursynth,
)

if enable_vsscript
    libvsscript = library('vapoursynth-script',
        files('src/vsscript/vsscript.cpp'),
        dependencies: py_dep,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
        install: true,
        soversion: run_command('grep', '-m', '1', 'VSSCRIPT_API_MAJOR', 'include/VSScript4.h', check: true).stdout().strip().split()[2],
    )

    pkg.generate(libvsscript,
        description: 'Library for interfacing VapourSynth with Python',
        filebase: 'vapoursynth-script',
        subdirs: 'vapoursynth',
    )

    vapoursynth_script_dep = declare_dependency(
        include_directories: incdir,
        link_with: libvsscript,
    )

    if enable_vspipe
        executable('vspipe',
            files(
                'src/common/wave.cpp',
                'src/vspipe/printgraph.cpp',
                'src/vspipe/vsjson.cpp',
                'src/vspipe/vspipe.cpp',
            ),
            dependencies: vapoursynth_script_dep,
            gnu_symbol_visibility: 'hidden',
            install: true,
            link_args: vspipe_link_args,
        )
    endif
elif enable_vspipe
    error('Cannot build vspipe when VSScript is disabled')
endif

if enable_python_module
    py.extension_module('vapoursynth',
        files('src/cython/vapoursynth.pyx'),
        install: true,
        link_with: libvapoursynth,
    )
endif
