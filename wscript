import sys, os, re, glob, subprocess
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
sys.path.insert(0, os.getcwd())
from waflib.Configure import conf
from waflib import Build, Utils
from waftools.checks.generic import *
from waftools.checks.custom import *

VERSION = '24'

libav_pkg_config_checks = [
    'libavutil',   '>= 52.3.0',
    'libavcodec',  '> 54.34.0',
    'libswscale',  '>= 2.0.0'
]

system_dependencies = [
    {
        'name': 'os-windows',
        'desc': 'Windows',
        'deps_any': ['os-win32', 'os-cygwin', 'os-msys', 'os-uwin'],
        'func': check_true
    },
    {
        'name': 'cpu-x86',
        'desc': 'x86 CPU',
        'func': check_cpu_x86
    },
    {
        'name': 'cpu-x86_64',
        'desc': 'x86_64 CPU',
        'func': check_cpu_x86_64
    }
]

main_dependencies = [
    {
        'name': 'noexecstack',
        'desc': 'compiler support for noexecstack',
        'global': True,
        'func': check_cc(linkflags='-Wl,-z,noexecstack')
    },
    {
        'name': 'bsymbolic',
        'desc': 'compiler support for -Wl,-Bsymbolic',
        'global': True,
        'func': check_cc(linkflags='-Wl,-Bsymbolic')
    },
    {
        'name': 'libdl',
        'desc': 'dynamic loader',
        'func': check_libs(['dl'], check_statement('dlfcn.h', 'dlopen("", 0)'))
    },
    {
        'name': 'dlopen',
        'desc': 'dlopen',
        'global': True,
        'deps_any': [ 'libdl', 'os-windows' ],
        'func': check_true
    },
    {
        'name': 'libm',
        'desc': '-lm',
        'global': True,
        'func': check_cc(lib='m')
    },
    {
        'name': 'python3',
        'desc': 'Python 3',
        'func': check_python
    },
    {
        'name': 'sphinx',
        'desc': 'Sphinx',
        'func': check_sphinx
    },
    {
        'name': 'ffmpeg',
        'desc': 'FFmpeg/libav',
        'func': check_pkg_config(*libav_pkg_config_checks)
    }
]

build_options = [
    {
        'name': '--shared',
        'desc': 'shared libraries',
        'default': 'enable',
        'func': check_true
    },
    {
        'name': '--static',
        'desc': 'static libraries',
        'default': 'disable',
        'func': check_true
    },
    {
        'name': '--debug',
        'desc': 'debug build',
        'default': 'disable',
        'func': check_true
    }
]

features = [
    {
        'name': '--vapoursynth',
        'desc': 'core library',
        'deps_any': ['shared', 'static'],
        'deps': ['dlopen', 'ffmpeg'],
        'default': 'enable',
        'func': check_true
    },
    {
        'name': '--avisynth-compat',
        'desc': 'AviSynth compatibility layer',
        'deps': ['vapoursynth', 'os-windows'],
        'func': check_true
    },
    {
        'name': '--vapoursynth-script',
        'desc': 'Python scripting',
        'deps_any': ['shared', 'static'],
        'deps': ['dlopen', 'python3'],
        'func': check_true
    },
    {
        'name': '--vspipe',
        'desc': 'VSPipe',
        'deps': ['vapoursynth', 'vapoursynth-script'],
        'func': check_true
    },
    {
        'name': '--filters',
        'desc': 'bundled filter plugins',
        'default': 'enable',
        'func': check_true
    },
    {
        'name': '--docs',
        'desc': 'HTML documentation',
        'deps': ['sphinx'],
        'func': check_true
    },
    {
        'name': '--examples',
        'desc': 'SDK examples',
        'default': 'disable',
        'func': check_true
    }
]

filter_dependencies = [
    {
        'name': 'ImageMagick++',
        'desc': 'ImageMagick++',
        'func': check_pkg_config('ImageMagick++')
    },
    {
        'name': 'libass',
        'desc': 'libass',
        'func': check_pkg_config('libass')
    },
    {
        'name': 'tesseract',
        'desc': 'Tesseract',
        'func': check_pkg_config('tesseract')
    }
]

filters = [
    {
        'name': '--assvapour',
        'desc': 'AssVapour',
        'deps': ['libass'],
        'func': check_true
    },
    {
        'name': '--eedi3',
        'desc': 'EEDI3',
        'func': check_true
    },
    {
        'name': '--imwri',
        'default': 'disable',
        'desc': 'ImageMagick Writer/Reader',
        'deps': ['ImageMagick++'],
        'func': check_true
    },
    {
        'name': '--morpho',
        'desc': 'Morpho',
        'func': check_true
    },
    {
        'name': '--ocr',
        'desc': 'OCR',
        'deps': ['tesseract'],
        'func': check_true
    },
    {
        'name': '--removegrain',
        'desc': 'RemoveGrain',
        'func': check_true
    },
    {
        'name': '--vinverse',
        'desc': 'Vinverse',
        'func': check_true
    },
    {
        'name': '--vivtc',
        'desc': 'VIVTC',
        'func': check_true
    }
]

_INSTALL_DIRS_LIST = [
    ('bindir',      '${PREFIX}/bin',                    'binary files'),
    ('includedir',  '${PREFIX}/include/vapoursynth',    'header files'),
    ('libdir',      '${PREFIX}/lib',                    'library files'),
    ('plugindir',   '${LIBDIR}/vapoursynth',            'plugins'),
    ('datadir',     '${PREFIX}/share',                  'data files'),
    ('docdir',      '${DATADIR}/doc/vapoursynth',       'documentation files')
]

def options(opt):
    opt.load('compiler_c')
    opt.load('compiler_cxx')
    opt.load('features')

    group = opt.get_option_group("build and install options")
    for ident, default, desc in _INSTALL_DIRS_LIST:
        group.add_option('--{0}'.format(ident),
            type    = 'string',
            dest    = ident,
            default = default,
            help    = 'directory for installing {0} [{1}]' \
                      .format(desc, default))

    opt.parse_features('build and install options', build_options)
    opt.parse_features('features', features)

    for f in filters:
        f['groups'] = (f.get('groups') or []) + ['filters']
    opt.parse_features('filters', filters)

@conf
def is_debug_build(ctx):
    return getattr(ctx.options, 'enable_debug')

def configure(ctx):
    ctx.check_waf_version(mini='1.7.15')

    ctx.env['VSVERSION'] = VERSION

    target = os.environ.get('TARGET')
    (cc, cxx, pkg_config) = ('cc', 'c++', 'pkg-config')

    if target:
        cc         = '-'.join([target, 'gcc'])
        cxx        = '-'.join([target, 'g++'])
        pkg_config = '-'.join([target, pkg_config])

    ctx.find_program(cc,                var='CC')
    ctx.find_program(cxx,               var='CXX')
    ctx.find_program(pkg_config,        var='PKG_CONFIG')

    for ident, _, _ in _INSTALL_DIRS_LIST:
        varname = ident.upper()
        ctx.env[varname] = getattr(ctx.options, ident)

        # keep substituting vars, until the paths are fully expanded
        while re.match('\$\{([^}]+)\}', ctx.env[varname]):
            ctx.env[varname] = Utils.subst_vars(ctx.env[varname], ctx.env)

        ctx.define("VS_PATH_" + varname, ctx.env[varname])

    ctx.load('compiler_c')
    ctx.load('compiler_cxx')
    ctx.load('dependencies')
    ctx.parse_dependencies(system_dependencies)
    ctx.load('detections.compiler')

    ctx.parse_dependencies(build_options)
    ctx.parse_dependencies(main_dependencies)
    ctx.parse_dependencies(features)
    ctx.parse_dependencies(filter_dependencies)
    ctx.parse_dependencies(filters)

    ctx.store_dependencies_lists()

    if ctx.dependency_satisfied("avisynth-compat"):
        ctx.define("VS_FEATURE_AVISYNTH", 1)

    if ctx.dependency_satisfied("os-windows"):
        ctx.define("VS_TARGET_OS_WINDOWS", 1)
    else:
        ctx.define("VS_TARGET_OS_" + ctx.env.DEST_OS.upper(), 1)

    if ctx.dependency_satisfied("cpu-x86"):
        cpu = 'x86'
    else:
        if ctx.env.DEST_CPU == 'ia':
            cpu = 'ia64'
        elif ctx.env.DEST_CPU in ['aarch64', 'thumb']:
            cpu = 'arm'
        elif ctx.env.DEST_CPU == 's390x':
            cpu = 's390'
        else:
            cpu = ctx.env.DEST_CPU

    ctx.define('VS_TARGET_CPU_' + cpu.upper(), 1)

def build(ctx):
    ctx.unpack_dependencies_lists()
    ctx.load('wscript_build')

def test(ctx):
    '''runs the Cython tests'''

    ctx.unpack_dependencies_lists()

    for name in glob.glob(os.path.join('test', '*.py')):
        env = os.environ.copy()

        if not ctx.dependency_satisfied('os-windows'):
            env['LD_LIBRARY_PATH'] = "{0}:{1}".format(ctx.env.LIBDIR, env['LD_LIBRARY_PATH'])

        code = subprocess.Popen([ctx.env.PYTHON[0], name], env = env).wait()

        if code != 0:
            ctx.fatal('Test {0} failed: {1}'.format(name, code))

class TestContext(Build.BuildContext):
    cmd = 'test'
    fun = 'test'
