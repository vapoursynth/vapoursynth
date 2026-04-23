project('VapourSynth', 'c', 'cpp',
    default_options: ['buildtype=release', 'b_lto=true', 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++17'],
    license: 'LGPL-2.1-or-later',
    license_files: 'COPYING.LESSER',
    meson_version: '>=1.3.0',
    version: '75rc1',
)

enable_guard_pattern = get_option('enable_guard_pattern')
enable_x86_asm = get_option('enable_x86_asm')
enable_core = get_option('enable_core')
enable_vsscript = get_option('enable_vsscript')
enable_vspipe = get_option('enable_vspipe')
enable_python_module = get_option('enable_python_module')
build_wheel = get_option('build_wheel')

if not build_wheel
    enable_vsscript = false
    enable_vspipe = false
    enable_python_module = false
endif

cxx = meson.get_compiler('cpp')

if cxx.get_argument_syntax() == 'gcc'
    avx2_args = '-march=x86-64-v3'
elif cxx.get_id() == 'clang-cl'
    avx2_args = '/clang:-march=x86-64-v3'
else
    avx2_args = '/arch:AVX2'
endif

host_system = host_machine.system()
is_win = host_system in ['cygwin', 'windows']

if is_win
    dl_dep = dependency('', required: false)
    libp2p_dep = dependency('libp2p', static: true)
else
    dl_dep = dependency('dl')
endif

if not host_machine.cpu_family().startswith('x86')
    enable_x86_asm = false
endif

incdir = include_directories('include')

lang = ['c', 'cpp']

pkg = import('pkgconfig')

py = import('python').find_installation(pure: false)
py_dep = py.dependency(version: is_win ? '>=3.14' : '>=3.12')  # Py_NO_LINK_LIB was introduced in 3.14

install_dir = build_wheel ? py.get_install_dir() / 'vapoursynth' : []

vs_current_release = run_command(
    py,
    '-c',
    'print(open("VAPOURSYNTH_VERSION").read())',
    check: true,
).stdout().strip().split()[2].split('-')[0]

if get_option('buildtype') == 'release'
    if cxx.get_id() == 'clang-cl'
        add_project_arguments('/clang:-flto', '/clang:-O3', language: lang)
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
    language: lang,
)

if is_win
    add_project_arguments('-DVS_TARGET_OS_WINDOWS', '-D_UNICODE', '-DUNICODE', language: lang)
elif host_system == 'darwin'
    add_project_arguments('-DVS_TARGET_OS_DARWIN', language: lang)
elif host_system in ['dragonfly', 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd']
    add_project_arguments('-DVS_TARGET_OS_LINUX', language: lang)
else
    warning('Unknown host OS')
endif

if enable_guard_pattern
    add_project_arguments('-DVS_FRAME_GUARD', language: lang)
endif

if enable_core
    deps = [
        dl_dep,
        dependency('threads'),
        dependency('zimg', static: true, version: '>=3.0.5'),
    ]

    libs = []

    soversion = is_win ? '' : run_command(
        'grep',
        '-m',
        '1',
        'VAPOURSYNTH_API_MAJOR',
        'include/VapourSynth4.h',
        check: true,
    ).stdout().strip().split()[2]

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

    if enable_x86_asm
        add_project_arguments('-DVS_TARGET_CPU_X86', language: lang)

        libs += static_library('core_sse2',
            files(
                'src/core/expr/jitcompiler_x86.cpp',
                'src/core/kernel/x86/average_sse2.c',
                'src/core/kernel/x86/convolution_sse2.cpp',
                'src/core/kernel/x86/generic_sse2.cpp',
                'src/core/kernel/x86/merge_sse2.c',
                'src/core/kernel/x86/planestats_sse2.c',
                'src/core/kernel/x86/transpose_sse2.c',
            ),
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
        )

        libs += static_library('core_avx2',
            files(
                'src/core/kernel/x86/convolution_avx2.cpp',
                'src/core/kernel/x86/generic_avx2.cpp',
                'src/core/kernel/x86/merge_avx2.c',
                'src/core/kernel/x86/planestats_avx2.c',
            ),
            c_args: avx2_args,
            cpp_args: avx2_args,
            gnu_symbol_visibility: 'hidden',
            include_directories: incdir,
        )
    endif

    libvapoursynth = library('libvapoursynth',
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
            'src/core/simplefilters.cpp',
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
        link_with: libs,
        name_prefix: '',
        soversion: soversion,
    )

    if not build_wheel
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

        pkg.generate(libvapoursynth,
            description: 'A frameserver for the 21st century',
            filebase: 'vapoursynth',
            name: 'vapoursynth',
            subdirs: 'vapoursynth',
        )
    endif

    vapoursynth_dep = declare_dependency(
        include_directories: incdir,
        link_with: libvapoursynth,
    )

    if enable_python_module
        add_languages('cython', native: false)

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
                'src/py/_utils.py',
                'src/py/vapoursynth.pyi',
            ),
            subdir: 'vapoursynth',
        )

        install_subdir(
            'include',
            exclude_directories: 'cython',
            exclude_files: ['VapourSynth.h', 'VSHelper.h'],
            install_dir: install_dir,
        )
    endif

    if build_wheel and cxx.get_argument_syntax() == 'msvc'
        avscompat = shared_module('avscompat',
            files('src/avisynth/avisynth_compat.cpp', 'src/avisynth/interface.cpp', 'src/core/cpufeatures.cpp'),
            cpp_args: '-DBUILDING_AVSCORE',
            dependencies: libp2p_dep,
            gnu_symbol_visibility: 'hidden',
            include_directories: [incdir, include_directories('src/avisynth')],
            install: true,
            install_dir: install_dir / 'plugins',
        )
    endif
elif enable_python_module
    error('Cannot build the Python module when the core is disabled')
endif

if enable_vsscript
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

    if enable_vspipe
        link_args = []

        if cxx.get_argument_syntax() == 'gcc' and is_win
            link_args += '-municode'
        elif cxx.get_argument_syntax() == 'msvc'
            link_args += [
                '/MANIFEST:EMBED',
                '/MANIFESTINPUT:' + (meson.project_source_root() / 'src/vspipe/vspipe.manifest'),
            ]
        endif

        executable('vspipe',
            files('src/common/wave.cpp', 'src/vspipe/printgraph.cpp', 'src/vspipe/vsjson.cpp', 'src/vspipe/vspipe.cpp'),
            dependencies: vsscript_dep,
            gnu_symbol_visibility: 'hidden',
            install: true,
            install_dir: install_dir,
            install_rpath: '$ORIGIN',
            link_args: link_args,
        )
    endif

    if build_wheel and cxx.get_argument_syntax() == 'msvc'
        vsvfw = library('vsvfw',
            files('src/common/fourcc.cpp', 'src/common/wave.cpp', 'src/vfw/vsvfw.cpp'),
            dependencies: [libp2p_dep, vsscript_dep],
            gnu_symbol_visibility: 'hidden',
            install: true,
            install_dir: install_dir,
            vs_module_defs: 'src/vfw/vsvfw.def',
        )
    endif
elif enable_vspipe
    error('Cannot build vspipe when VSScript is disabled')
endif
