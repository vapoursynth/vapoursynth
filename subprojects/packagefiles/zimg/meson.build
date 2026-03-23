project('zimg', 'cpp',
    default_options: ['cpp_std=c++17'],
    version: '3.0.6',
)

if meson.get_compiler('cpp').get_argument_syntax() == 'gcc'
    avx2_args = '-march=x86-64-v3'
    avx512_args = '-march=x86-64-v4'
    avx512vnni_args = '-mavx512vnni'
else
    avx2_args = '/arch:AVX2'
    avx512_args = '/arch:AVX512'
    avx512vnni_args = []
endif

incdir = include_directories('graphengine/include', 'src/zimg')

libs = []

add_project_arguments('-DGRAPHENGINE_IMPL_NAMESPACE=zimg', '-DZIMG_GRAPHENGINE_API', language: 'cpp')

if host_machine.cpu_family() == 'aarch64'
    add_project_arguments('-DZIMG_ARM', language: 'cpp')

    libs += static_library('zimg_arm',
        files(
            'src/zimg/colorspace/arm/operation_impl_arm.cpp',
            'src/zimg/colorspace/arm/operation_impl_neon.cpp',
            'src/zimg/common/arm/cpuinfo_arm.cpp',
            'src/zimg/common/arm/neon_util.cpp',
            'src/zimg/depth/arm/depth_convert_arm.cpp',
            'src/zimg/depth/arm/depth_convert_neon.cpp',
            'src/zimg/depth/arm/dither_arm.cpp',
            'src/zimg/depth/arm/dither_neon.cpp',
            'src/zimg/depth/arm/error_diffusion_neon.cpp',
            'src/zimg/resize/arm/resize_impl_arm.cpp',
            'src/zimg/resize/arm/resize_impl_neon.cpp',
        ),
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )
elif host_machine.cpu_family().startswith('x86')
    add_project_arguments('-DZIMG_X86', language: 'cpp')

    libs += static_library('zimg_x86',
        files(
            'src/zimg/colorspace/x86/operation_impl_x86.cpp',
            'src/zimg/common/x86/cpuinfo_x86.cpp',
            'src/zimg/common/x86/x86util.cpp',
            'src/zimg/depth/x86/depth_convert_x86.cpp',
            'src/zimg/depth/x86/dither_x86.cpp',
            'src/zimg/resize/x86/resize_impl_x86.cpp',
            'src/zimg/unresize/x86/unresize_impl_x86.cpp',
        ),
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )

    libs += static_library('zimg_avx2',
        files(
            'src/zimg/colorspace/x86/operation_impl_avx2.cpp',
            'src/zimg/depth/x86/depth_convert_avx2.cpp',
            'src/zimg/depth/x86/dither_avx2.cpp',
            'src/zimg/depth/x86/error_diffusion_avx2.cpp',
            'src/zimg/resize/x86/resize_impl_avx2.cpp',
            'src/zimg/unresize/x86/unresize_impl_avx2.cpp',
        ),
        cpp_args: avx2_args,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )

    libs += static_library('zimg_avx512',
        files(
            'src/zimg/colorspace/x86/gamma_constants_avx512.cpp',
            'src/zimg/colorspace/x86/operation_impl_avx512.cpp',
            'src/zimg/depth/x86/depth_convert_avx512.cpp',
            'src/zimg/depth/x86/dither_avx512.cpp',
            'src/zimg/resize/x86/resize_impl_avx512.cpp',
        ),
        cpp_args: avx512_args,
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )

    libs += static_library('zimg_avx512vnni',
        files('src/zimg/resize/x86/resize_impl_avx512_vnni.cpp'),
        cpp_args: [avx512_args, avx512vnni_args],
        gnu_symbol_visibility: 'hidden',
        include_directories: incdir,
    )
endif

zimg = library('zimg',
    files(
        'graphengine/graphengine/x86/cpuinfo_x86.cpp',
        'graphengine/graphengine/cpuinfo.cpp',
        'graphengine/graphengine/graph.cpp',
        'graphengine/graphengine/node.cpp',
        'src/zimg/api/zimg.cpp',
        'src/zimg/colorspace/colorspace.cpp',
        'src/zimg/colorspace/colorspace_graph.cpp',
        'src/zimg/colorspace/colorspace_param.cpp',
        'src/zimg/colorspace/gamma.cpp',
        'src/zimg/colorspace/matrix3.cpp',
        'src/zimg/colorspace/operation.cpp',
        'src/zimg/colorspace/operation_impl.cpp',
        'src/zimg/common/cpuinfo.cpp',
        'src/zimg/common/libm_wrapper.cpp',
        'src/zimg/common/matrix.cpp',
        'src/zimg/depth/blue.cpp',
        'src/zimg/depth/depth.cpp',
        'src/zimg/depth/depth_convert.cpp',
        'src/zimg/depth/dither.cpp',
        'src/zimg/depth/quantize.cpp',
        'src/zimg/graph/filter_base.cpp',
        'src/zimg/graph/filtergraph.cpp',
        'src/zimg/graph/graphbuilder.cpp',
        'src/zimg/graph/graphengine_except.cpp',
        'src/zimg/graph/simple_filters.cpp',
        'src/zimg/resize/filter.cpp',
        'src/zimg/resize/resize.cpp',
        'src/zimg/resize/resize_impl.cpp',
        'src/zimg/unresize/bilinear.cpp',
        'src/zimg/unresize/unresize.cpp',
        'src/zimg/unresize/unresize_impl.cpp',
    ),
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    link_with: libs,
)

zimg_dep = declare_dependency(
    include_directories: include_directories('src/zimg/api'),
    link_with: zimg,
)

meson.override_dependency('zimg', zimg_dep)
