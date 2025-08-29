project('vapoursynth', ['c', 'cpp'],
    version: run_command('python3', '-c', 
    'import re; print(re.search(r"#define VS_CURRENT_RELEASE (\\d+)", open("VAPOURSYNTH_VERSION").read()).group(1))', 
    check: true).stdout().strip(),
  license : 'LGPL-2.1+',
  default_options : [
    'warning_level=3',
    'cpp_std=c++17',
    'c_std=c99',
    'optimization=3'
  ]
)

version_parts = meson.project_version().split('-')
current_release = version_parts[0]

host_system = host_machine.system()
host_cpu = host_machine.cpu_family()

x86 = host_cpu in ['x86', 'x86_64']
ppc = host_cpu.startswith('ppc')
arm = host_cpu in ['arm', 'aarch64']
bits = host_cpu == 'x86_64' ? '64' : '32'

thread_dep = dependency('threads')

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')


warning_flags = [
  '-Wno-unused-parameter',
  '-Wno-unused-function',
  '-Wno-ignored-attributes'
]


common_flags = [
  '-fvisibility=hidden'
] + warning_flags

if x86
  common_flags += ['-mfpmath=sse', '-msse2']
  avx2_flags = ['-mavx2', '-mfma', '-mtune=haswell']
endif

undefined_ldflags = []
plugin_ldflags = []
unicode_cflags = []
unicode_ldflags = []
stackrealign_flags = []
python_module_undefined = []
maybe_python3_libs = []

if host_system == 'darwin'
  add_project_arguments('-DVS_TARGET_OS_DARWIN', language: ['c', 'cpp'])
  undefined_ldflags = ['-Wl,-undefined,error']
  python_module_undefined = ['-undefined', 'dynamic_lookup']
elif host_system in ['linux', 'freebsd', 'openbsd', 'netbsd', 'dragonfly']
  add_project_arguments('-DVS_TARGET_OS_LINUX', language: ['c', 'cpp'])
  undefined_ldflags = ['-Wl,--no-undefined']
elif host_system == 'windows'
  add_project_arguments('-DVS_TARGET_OS_WINDOWS', language: ['c', 'cpp'])
  unicode_cflags = ['-DUNICODE', '-D_UNICODE']
  unicode_ldflags = ['-municode']
  if bits == '32'
    plugin_ldflags = ['-Wl,--kill-at']
    stackrealign_flags = ['-mstackrealign']
  endif
  python_module_undefined = ['-no-undefined']
else
  error('Unknown host OS: ' + host_system)
endif

if x86
  add_project_arguments('-DVS_TARGET_CPU_X86', language: ['c', 'cpp'])
elif ppc
  add_project_arguments('-DVS_TARGET_CPU_POWERPC', language: ['c', 'cpp'])
elif arm
  add_project_arguments('-DVS_TARGET_CPU_ARM', language: ['c', 'cpp'])
endif

vs_debug = get_option('vs_debug')
static_build = get_option('static_build')
guard_pattern = get_option('guard_pattern')
core_enabled = get_option('core')
x86_asm = get_option('x86_asm')
vsscript = get_option('vsscript')
vspipe = get_option('vspipe')
python_module = get_option('python_module')

debug_flags = []
if vs_debug
  add_project_arguments('-DVS_CORE_DEBUG', language: ['c', 'cpp'])
  debug_flags = ['-O0', '-g3', '-ggdb', '-ftrapv']
else
  add_project_arguments('-DNDEBUG', language: ['c', 'cpp'])
endif

if guard_pattern
  add_project_arguments('-DVS_FRAME_GUARD', language: ['c', 'cpp'])
endif

plugindir = get_option('plugindir')
if plugindir == ''
  plugindir = get_option('libdir') / 'vapoursynth'
endif

add_project_arguments(common_flags + unicode_cflags + stackrealign_flags + debug_flags, 
                      language: ['c', 'cpp'])

inc_dir = include_directories('include')

zimg_dep = dependency('zimg', version: '>= 3.0.5', required: core_enabled)
python3_dep = dependency('python3', required: vsscript or python_module)
python3_embed_dep = dependency('python3-embed', required: false)

if python3_embed_dep.found()
  python_dep_to_use = python3_embed_dep
else
  python_dep_to_use = python3_dep
endif


dlopen_lib = []
if host_system != 'windows'
  dlopen_lib = cc.find_library('dl', required: false)
  if not dlopen_lib.found()
    dlopen_lib = cc.find_library('dld', required: true)
  endif
endif


have_sched_getaffinity = cxx.links('''
#define _GNU_SOURCE
#include <sched.h>
int main() {
  cpu_set_t affinity;
  sched_getaffinity(0, sizeof(cpu_set_t), &affinity);
  int count = CPU_COUNT(&affinity);
  return 0;
}
''', name: 'sched_getaffinity')

if have_sched_getaffinity
  add_project_arguments('-DHAVE_SCHED_GETAFFINITY', language: ['c', 'cpp'])
endif


have_cpuset_getaffinity = cxx.links('''
#include <sys/param.h>
#include <sys/_cpuset.h>
#include <sys/cpuset.h>
int main() {
  cpuset_t affinity;
  cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), &affinity);
  int count = CPU_COUNT(&affinity);
  return 0;
}
''', name: 'cpuset_getaffinity')

if have_cpuset_getaffinity
  add_project_arguments('-DHAVE_CPUSET_GETAFFINITY', language: ['c', 'cpp'])
endif


cython = find_program('cython3', 'cython', required: python_module)

# Python version handling
if vsscript or python_module
  python3 = import('python').find_installation('python3')
  python_version = python3.language_version()
  if host_system == 'windows'
    maybe_python3_libs = python_dep_to_use
  endif
endif

vapoursynth_lib = []
if core_enabled
  add_project_arguments([
    '-DVS_CORE_EXPORTS',
    '-DVS_USE_LATEST_API', 
    '-DVS_GRAPH_API',
    '-DVS_PATH_PLUGINDIR="@0@"'.format(plugindir),
    '-DVS_CURRENT_RELEASE=@0@'.format(current_release)
  ], language: ['c', 'cpp'])

  if not meson.is_cross_build()
    zimg_version_check = cxx.run('''
#include <zimg.h>
#include <iostream>
#if ZIMG_API_VERSION < ZIMG_MAKE_API_VERSION(2, 4)
#error zimg API v2.4 or greater required
#endif
int main() {
  unsigned major, minor, micro;
  zimg_get_version_info(&major, &minor, &micro);
  return 0;
}
    ''', dependencies: zimg_dep)
    
    if zimg_version_check.returncode() != 0
      error('zimg API v2.4 or greater required')
    endif
  else
    zimg_compile_check = cxx.compiles('''
#include <zimg.h>
#if ZIMG_API_VERSION < ZIMG_MAKE_API_VERSION(2, 4)
#error zimg API v2.4 or greater required
#endif
int main() {
  unsigned major, minor, micro;
  zimg_get_version_info(&major, &minor, &micro);
  return 0;
}
    ''', dependencies: zimg_dep, name: 'zimg API version check')
    
    if not zimg_compile_check
      error('zimg API v2.4 or greater required')
    endif
  endif

  # Core sources
  core_sources = [
    'src/core/audiofilters.cpp',
    'src/core/averageframesfilter.cpp', 
    'src/core/boxblurfilter.cpp',
    'src/core/cpufeatures.cpp',
    'src/core/expr/expr.cpp',
    'src/core/expr/jitcompiler.cpp',
    'src/core/exprfilter.cpp',
    'src/core/genericfilters.cpp',
    'src/core/kernel/average.cpp',
    'src/core/kernel/cpulevel.cpp',
    'src/core/kernel/generic.cpp',
    'src/core/kernel/merge.c',
    'src/core/kernel/planestats.c',
    'src/core/kernel/transpose.c',
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
    'src/core/vsthreadpool.cpp'
  ]

  if x86 and x86_asm
    core_sources += [
      'src/core/expr/jitcompiler_x86.cpp',
      'src/core/kernel/x86/average_sse2.c',
      'src/core/kernel/x86/convolution_sse2.cpp',
      'src/core/kernel/x86/generic_sse2.cpp',
      'src/core/kernel/x86/merge_sse2.c',
      'src/core/kernel/x86/planestats_sse2.c',
      'src/core/kernel/x86/transpose_sse2.c'
    ]
  endif

  vapoursynth_deps = [thread_dep, zimg_dep]
  if dlopen_lib.found()
    vapoursynth_deps += dlopen_lib
  endif

  vapoursynth_link_with = []
  if x86 and x86_asm
    avx2_sources = [
      'src/core/kernel/x86/convolution_avx2.cpp',
      'src/core/kernel/x86/generic_avx2.cpp', 
      'src/core/kernel/x86/merge_avx2.c',
      'src/core/kernel/x86/planestats_avx2.c'
    ]
    
    avx2_lib = static_library('vapoursynth_avx2',
      avx2_sources,
      c_args: avx2_flags,
      cpp_args: avx2_flags,
      include_directories: inc_dir
    )
    
    vapoursynth_link_with = [avx2_lib]
  endif

  if static_build
    vapoursynth_lib = static_library('vapoursynth',
      core_sources,
      include_directories: inc_dir,
      dependencies: vapoursynth_deps,
      link_with: vapoursynth_link_with,
      install: true
    )
  else
    vapoursynth_lib = shared_library('vapoursynth',
      core_sources,
      include_directories: inc_dir,
      dependencies: vapoursynth_deps,
      link_with: vapoursynth_link_with,
      link_args: undefined_ldflags,
      install: true,
      version: '0.0.0'
    )
  endif

  # Install headers
  install_headers([
    'include/VapourSynth.h',
    'include/VapourSynth4.h', 
    'include/VSConstants4.h',
    'include/VSHelper.h',
    'include/VSHelper4.h',
    'include/VSScript4.h'
  ], subdir: 'vapoursynth')

  # Generate pkg-config file
  pkg = import('pkgconfig')
  pkg.generate(vapoursynth_lib,
    name: 'vapoursynth',
    description: 'A frameserver for the 21st century',
    url: 'http://www.vapoursynth.com/'
  )
endif

vsscript_lib = []
if vsscript
  add_project_arguments([
    '-DVS_USE_LATEST_API',
    '-DVSSCRIPT_USE_LATEST_API',
    '-DVS_CURRENT_RELEASE=@0@'.format(current_release)
  ], language: ['c', 'cpp'])

  vsscript_lib = library('vapoursynth-script',
    'src/vsscript/vsscript.cpp',
    include_directories: inc_dir,
    dependencies: [python_dep_to_use],
    link_args: static_build ? [] : undefined_ldflags,
    install: true,
    version: static_build ? undefined : '0.0.0'
  )

  pkg.generate(vsscript_lib,
    name: 'vapoursynth-script',
    description: 'VapourSynth script interface',
    url: 'http://www.vapoursynth.com/'
  )
endif

if vspipe and vsscript
  if not core_enabled
    add_project_arguments([
      '-DVS_USE_LATEST_API',
      '-DVS_GRAPH_API', 
      '-DVSSCRIPT_USE_LATEST_API'
    ], language: ['c', 'cpp'])
  endif

  vspipe_sources = [
    'src/vspipe/vspipe.cpp',
    'src/vspipe/printgraph.cpp',
    'src/vspipe/vsjson.cpp',
    'src/common/wave.cpp'
  ]

  executable('vspipe',
    vspipe_sources,
    include_directories: inc_dir,
    dependencies: [python_dep_to_use],
    link_with: [vsscript_lib],
    link_args: unicode_ldflags,
    cpp_args: ['-DVS_CURRENT_RELEASE=@0@'.format(current_release)],
    install: true
  )
endif

if python_module and core_enabled
  add_project_arguments([
    '-DVS_CURRENT_RELEASE=@0@'.format(current_release)
  ], language: ['c', 'cpp'])

  vapoursynth_c = custom_target('vapoursynth_c',
    input: 'src/cython/vapoursynth.pyx',
    output: 'vapoursynth.c',
    command: [cython, '-o', '@OUTPUT@', '@INPUT@']
  )

  python_deps = [python_dep_to_use]
  python_link_with = [vapoursynth_lib]
  python_link_args = python_module_undefined

  if host_system == 'windows' and maybe_python3_libs.found()
    python_deps += maybe_python3_libs
  endif

  python3.extension_module('vapoursynth',
    vapoursynth_c,
    include_directories: inc_dir,
    dependencies: python_deps,
    link_with: python_link_with,
    link_args: python_link_args,
    c_args: ['-fvisibility=default', '-Wno-implicit-fallthrough'],
    install: true
  )
endif
