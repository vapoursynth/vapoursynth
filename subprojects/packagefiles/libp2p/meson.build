project('libp2p', 'cpp',
    default_options: ['cpp_std=c++14'],
)

cxx = meson.get_compiler('cpp')

if cxx.get_argument_syntax() == 'gcc'
    add_project_arguments('-w', language: 'cpp')
else
    add_project_arguments('/W0', language: 'cpp')
endif

if get_option('buildtype') == 'release'
    if cxx.get_id() == 'msvc'
        add_project_arguments('/GL', language: 'cpp')
    elif cxx.get_id() == 'clang-cl'
        add_project_arguments('/clang:-flto', '/clang:-O3', language: 'cpp')
    endif

    if cxx.get_linker_id() == 'link'
        add_project_link_arguments('/LTCG', language: 'cpp')
    endif
endif

libs = []

if meson.get_compiler('cpp').get_argument_syntax() == 'gcc'
    sse4_args = '-march=x86-64-v2'
elif meson.get_compiler('cpp').get_id() == 'clang-cl'
    sse4_args = '/clang:-march=x86-64-v2'
else
    sse4_args = []
endif

if host_machine.cpu_family().startswith('x86')
    add_project_arguments('-DP2P_SIMD', language: 'cpp')

    libs += static_library('p2p_sse41',
        files('simd/p2p_sse41.cpp'),
        cpp_args: sse4_args,
        gnu_symbol_visibility: 'hidden',
    )
endif

libp2p = library('p2p',
    files('simd/cpuinfo_x86.cpp', 'simd/p2p_simd.cpp', 'p2p_api.cpp', 'v210.cpp'),
    gnu_symbol_visibility: 'hidden',
    link_with: libs,
)

libp2p_dep = declare_dependency(
    include_directories: include_directories('.'),
    link_with: libp2p,
)

meson.override_dependency('libp2p', libp2p_dep)
