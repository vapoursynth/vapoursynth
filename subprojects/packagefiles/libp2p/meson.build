project('libp2p', 'cpp',
    default_options: ['cpp_std=c++14'],
)

libs = []

p2p_args = []

sse4_args = meson.get_compiler('cpp').get_argument_syntax() == 'gcc' ? '-march=x86-64-v2' : []

if host_machine.cpu_family().startswith('x86')
    p2p_args += '-DP2P_SIMD'

    libs += static_library('p2p_sse41',
        files('simd/p2p_sse41.cpp'),
        cpp_args: p2p_args + sse4_args,
        gnu_symbol_visibility: 'hidden',
    )
endif

libp2p = library('p2p',
    files('simd/cpuinfo_x86.cpp', 'simd/p2p_simd.cpp', 'p2p_api.cpp', 'v210.cpp'),
    cpp_args: p2p_args,
    gnu_symbol_visibility: 'hidden',
    link_with: libs,
)

libp2p_dep = declare_dependency(
    include_directories: include_directories('.'),
    link_with: libp2p,
)

meson.override_dependency('libp2p', libp2p_dep)
