# Meson port of glslang for VapourSynth's runtime GLSL to SPIR-V compilation.
# Deliberately minimal: the single merged 16.x library with the C interface and the
# default resource limits, no optimizer (ENABLE_OPT=0, so neither SPIRV-Tools nor
# SPIRV-Headers is needed), no HLSL frontend (ENABLE_HLSL stays undefined) and no
# standalone tools. Bumping the version means updating the wrap hash and rechecking
# this file list against the upstream CMakeLists.
project('glslang', 'cpp',
    version: '16.4.0',
    license: 'BSD-3-Clause AND BSD-2-Clause AND MIT AND Apache-2.0',
    default_options: ['cpp_std=c++17', 'warning_level=0'],
    meson_version: '>=0.63.0',
)

cpp = meson.get_compiler('cpp')

# Generates glslang/build_info.h in the build tree; the template's @major@ style
# placeholders are exactly configure_file syntax.
subdir('glslang')

glslang_args = ['-DENABLE_OPT=0']
if cpp.get_argument_syntax() == 'msvc'
    glslang_args += '/bigobj'
endif

glslang_sources = files(
    'glslang/CInterface/glslang_c_interface.cpp',
    'glslang/GenericCodeGen/CodeGen.cpp',
    'glslang/GenericCodeGen/Link.cpp',
    'glslang/MachineIndependent/Constant.cpp',
    'glslang/MachineIndependent/InfoSink.cpp',
    'glslang/MachineIndependent/Initialize.cpp',
    'glslang/MachineIndependent/IntermTraverse.cpp',
    'glslang/MachineIndependent/Intermediate.cpp',
    'glslang/MachineIndependent/ParseContextBase.cpp',
    'glslang/MachineIndependent/ParseHelper.cpp',
    'glslang/MachineIndependent/PoolAlloc.cpp',
    'glslang/MachineIndependent/RemoveTree.cpp',
    'glslang/MachineIndependent/Scan.cpp',
    'glslang/MachineIndependent/ShaderLang.cpp',
    'glslang/MachineIndependent/SpirvIntrinsics.cpp',
    'glslang/MachineIndependent/SymbolTable.cpp',
    'glslang/MachineIndependent/Versions.cpp',
    'glslang/MachineIndependent/attribute.cpp',
    'glslang/MachineIndependent/glslang_tab.cpp',
    'glslang/MachineIndependent/intermOut.cpp',
    'glslang/MachineIndependent/iomapper.cpp',
    'glslang/MachineIndependent/limits.cpp',
    'glslang/MachineIndependent/linkValidate.cpp',
    'glslang/MachineIndependent/parseConst.cpp',
    'glslang/MachineIndependent/preprocessor/Pp.cpp',
    'glslang/MachineIndependent/preprocessor/PpAtom.cpp',
    'glslang/MachineIndependent/preprocessor/PpContext.cpp',
    'glslang/MachineIndependent/preprocessor/PpScanner.cpp',
    'glslang/MachineIndependent/preprocessor/PpTokens.cpp',
    'glslang/MachineIndependent/propagateNoContraction.cpp',
    'glslang/MachineIndependent/reflection.cpp',
    'glslang/ResourceLimits/ResourceLimits.cpp',
    'glslang/ResourceLimits/resource_limits_c.cpp',
    'SPIRV/CInterface/spirv_c_interface.cpp',
    'SPIRV/GlslangToSpv.cpp',
    'SPIRV/InReadableOrder.cpp',
    'SPIRV/Logger.cpp',
    'SPIRV/SpvBuilder.cpp',
    'SPIRV/SpvPostProcess.cpp',
    'SPIRV/SpvTools.cpp',
    'SPIRV/disassemble.cpp',
    'SPIRV/doc.cpp',
)

if host_machine.system() == 'windows'
    glslang_sources += files('glslang/OSDependent/Windows/ossource.cpp')
    os_deps = []
else
    glslang_sources += files('glslang/OSDependent/Unix/ossource.cpp')
    os_deps = [dependency('threads')]
endif

# '.' resolves to both the source root (public headers under glslang/ and SPIRV/) and
# the build root (the generated glslang/build_info.h).
glslang_incdir = include_directories('.')

glslang_lib = static_library('glslang',
    glslang_sources,
    build_info_h,
    cpp_args: glslang_args,
    dependencies: os_deps,
    gnu_symbol_visibility: 'hidden',
    include_directories: glslang_incdir,
)

glslang_dep = declare_dependency(
    include_directories: glslang_incdir,
    link_with: glslang_lib,
    sources: build_info_h,
)

meson.override_dependency('glslang', glslang_dep)
