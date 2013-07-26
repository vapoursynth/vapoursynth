import glob, os, subprocess, sys
from waflib import Build, Task, TaskGen, Utils

APPNAME = 'VapourSynth'
VERSION = '19'

TOP = os.curdir
OUT = 'build'

class docs(Task.Task):
    "Build Sphinx documentation"

    ext_out = ['.html']
    inst_to = None
    color = 'PINK'

    def run(self):
        subprocess.Popen('make html BUILDDIR={0}'.format(os.path.join(os.pardir, OUT)),
                         shell = True,
                         cwd = 'doc',
                         stdout = subprocess.PIPE).wait()

@TaskGen.feature('docs')
@TaskGen.before_method('process_source')
def apply_rst(self):
    rst_nodes = []
    no_nodes = []

    for x in self.to_nodes(self.source):
        if x.name.endswith('.rst'):
            rst_nodes.append(x)
        else:
            no_nodes.append(x)

    self.source = no_nodes

    inst = getattr(self, 'install_path', '${DOCDIR}')
    mod = getattr(self, 'chmod', Utils.O644)

    bld_nodes = []
    i = 0

    for node in rst_nodes:
        n = self.path.find_node(OUT).make_node('html')

        cur = node.parent
        dirs = []

        while not cur is self.path.find_node('doc'):
            dirs.append(cur)
            cur = cur.parent

        for dir in reversed(dirs):
            n = n.make_node(dir.name)

        n = n.make_node(node.name).change_ext('.html')

        bld_nodes.append(n)

        if inst:
            path = inst

            for dir in reversed(dirs):
                path = os.path.join(path, dir.name)

            setattr(self, 'install_task_{0}'.format(i), self.bld.install_files(path, n, env = self.env, chmod = mod))

        i += 1

    self.rst_task = self.create_task('docs', rst_nodes, bld_nodes)

def options(opt):
    opt.load('compiler_c')
    opt.load('compiler_cxx')
    opt.load('qt4')

    opt.add_option('--libdir', action = 'store', default = '${PREFIX}/lib', help = 'library installation directory')
    opt.add_option('--plugindir', action = 'store', default = '${LIBDIR}/vapoursynth', help = 'plugin installation directory')
    opt.add_option('--docdir', action = 'store', default = '${PREFIX}/share/doc/vapoursynth', help = 'documentation installation directory')
    opt.add_option('--includedir', action = 'store', default = '${PREFIX}/include/vapoursynth', help = 'header installation directory')

    opt.add_option('--mode', action = 'store', default = 'release', help = 'the mode to compile in (debug/release)')

    opt.add_option('--shared', action = 'store', default = 'true', help = 'build shared libraries (true/false)')
    opt.add_option('--static', action = 'store', default = 'false', help = 'build static libraries (true/false)')

    opt.add_option('--core', action = 'store', default = 'true', help = 'build the libvapoursynth library (true/false)')
    opt.add_option('--avisynth', action = 'store', default = 'true', help = 'build Avisynth compatibility layer (true/false)')
    opt.add_option('--script', action = 'store', default = 'true', help = 'build the libvapoursynth-script library (true/false)')
    opt.add_option('--pipe', action = 'store', default = 'true', help = 'build the vspipe program (true/false)')

    opt.add_option('--filters', action = 'store', default = 'true', help = 'build included filters (true/false)')
    opt.add_option('--examples', action = 'store', default = 'false', help = 'install SDK examples (true/false)')

    opt.add_option('--docs', action = 'store', default = 'false', help = 'build the documentation (true/false)')

def configure(conf):
    def add_options(flags, options):
        for flag in flags:
            conf.env.append_unique(flag, options)

    conf.load('compiler_c')
    conf.load('compiler_cxx')

    # Normalize OS.
    os = 'windows' if conf.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin'] else conf.env.DEST_OS

    conf.define('VS_TARGET_OS_' + os.upper(), 1)
    conf.msg('Settting DEST_OS to', conf.env.DEST_OS)

    # Normalize CPU values.
    if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
        cpu = 'x86'
    elif conf.env.DEST_CPU == 'ia':
        cpu = 'ia64'
    elif conf.env.DEST_CPU in ['aarch64', 'thumb']:
        cpu = 'arm'
    elif conf.env.DEST_CPU == 's390x':
        cpu = 's390'
    else:
        cpu = conf.env.DEST_CPU

    conf.define('VS_TARGET_CPU_' + cpu.upper(), 1)
    conf.msg('Settting DEST_CPU to', conf.env.DEST_CPU)

    # No need to sanitize BINFMT.
    conf.define('VS_TARGET_BINFMT_' + conf.env.DEST_BINFMT.upper(), 1)
    conf.msg('Settting DEST_BINFMT to', conf.env.DEST_BINFMT)

    for x in ['shared',
              'static',
              'core',
              'avisynth',
              'script',
              'pipe',
              'filters',
              'examples',
              'docs']:
        val = conf.options.__dict__[x]

        if not val in ['true', 'false']:
            conf.fatal('--{0} must be either true or false.'.format(x))
        else:
            u = x.upper()

            conf.env[u] = val
            conf.define('VS_FEATURE_' + u, 1 if val == 'true' else 0)

    if conf.env.CORE == 'false':
        conf.env.AVISYNTH = 'false'
        conf.env.SCRIPT = 'false'

    if 'false' in [conf.env.CORE, conf.env.SCRIPT]:
        conf.env.PIPE = 'false'

    if conf.env.CORE == 'false' and \
       conf.env.SCRIPT == 'false' and \
       conf.env.FILTERS == 'false' and \
       conf.env.EXAMPLES == 'false':
        conf.env.SHARED = 'false'
        conf.env.STATIC = 'false'
    else:
        if (conf.env.SHARED, conf.env.STATIC) == ('false', 'false') and not have_libs:
            conf.fatal('--static and --shared cannot both be false.')

    for (x, y) in [('SHARED', 'shared libraries'),
                   ('STATIC', 'static libraries'),
                   ('CORE', 'libvapoursynth'),
                   ('AVISYNTH', 'Avisynth compatibility'),
                   ('SCRIPT', 'libvapoursynth-script'),
                   ('PIPE', 'vspipe'),
                   ('FILTERS', 'included filters'),
                   ('EXAMPLES', 'plugin examples'),
                   ('DOCS', 'documentation')]:
        conf.msg('Enabling {0}?'.format(y), 'yes' if conf.env[x] == 'true' else 'no')

    conf.define('VS_PATH_PREFIX', conf.env.PREFIX)
    conf.msg('Setting PREFIX to', conf.env.PREFIX)

    for dir in ['libdir', 'plugindir', 'docdir', 'includedir']:
        u = dir.upper()

        conf.env[u] = Utils.subst_vars(conf.options.__dict__[dir], conf.env)
        conf.define('VS_PATH_' + u, conf.env[u])
        conf.msg('Setting {0} to'.format(u), conf.env[u])

    if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
        # Load Yasm explicitly, then the Nasm module which
        # supports both Nasm and Yasm.
        conf.find_program('yasm', var = 'AS', mandatory = True)
        conf.load('nasm')

        add_options(['ASFLAGS'],
                    ['-w',
                     '-Worphan-labels',
                     '-Wunrecognized-char',
                     '-Dprogram_name=vs'])
    else:
        # For all non-x86 targets, use the GNU assembler.
        # Waf uses GCC instead of the assembler directly.
        conf.load('gas')

    if conf.env.DEST_OS == 'darwin' and conf.env.DEST_CPU in ['x86', 'x86_64']:
        if conf.env.CXX_NAME == 'gcc':
            add_options(['ASFLAGS'],
                        ['-DPREFIX=1'])

    if conf.env.CXX_NAME == 'gcc':
        add_options(['CFLAGS', 'CXXFLAGS'],
                    ['-DVS_CORE_EXPORTS',
                     '-fPIC'])
    elif conf.env.CXX_NAME == 'msvc':
        add_options(['CFLAGS', 'CXXFLAGS'],
                    ['/DVS_CORE_EXPORTS',
                     '/EHsc',
                     '/Zc:wchar_t-'])

    if conf.env.DEST_CPU in ['x86_64', 'x64', 'amd64', 'x86_amd64']:
        add_options(['ASFLAGS'],
                    ['-DARCH_X86_64=1',
                     '-DPIC=1'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho64'
        elif conf.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin']:
            fmt = 'win64'
        else:
            fmt = 'elf64'
    elif conf.env.DEST_CPU == 'x86':
        add_options(['ASFLAGS'],
                    ['-DARCH_X86_64=0'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho32'
        elif conf.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin']:
            fmt = 'win32'
        else:
            fmt = 'elf32'

    if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
        add_options(['ASFLAGS'],
                    ['-f{0}'.format(fmt)])

    if conf.options.mode == 'debug':
        if conf.env.CXX_NAME == 'gcc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['-DVS_CORE_DEBUG',
                         '-g',
                         '-ggdb',
                         '-ftrapv'])
        elif conf.env.CXX_NAME == 'msvc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['/DVS_CORE_DEBUG',
                         '/Z7'])

        add_options(['ASFLAGS'],
                    ['-DVS_CORE_DEBUG'])

        if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
            if conf.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin']:
                dbgfmt = 'cv8'
            else:
                dbgfmt = 'dwarf2'

            add_options(['ASFLAGS'],
                        ['-g{0}'.format(dbgfmt)])
        else:
            add_options(['ASFLAGS'],
                        ['-Wa,-g'])
    elif conf.options.mode == 'release':
        if conf.env.CXX_NAME == 'gcc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['-O3'])
        elif conf.env.CXX_NAME == 'msvc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['/Ox'])
    else:
        conf.fatal('--mode must be either debug or release.')

    # Waf always uses gcc/g++ for linking when using a GCC
    # compatible C/C++ compiler.
    if conf.env.CXX_NAME == 'gcc':
        if not conf.env.DEST_OS in ['darwin', 'win32', 'cygwin', 'msys', 'uwin']:
            add_options(['LINKFLAGS_cshlib',
                         'LINKFLAGS_cprogram',
                         'LINKFLAGS_cxxshlib',
                         'LINKFLAGS_cxxprogram'],
                        ['-Wl,-Bsymbolic',
                         '-Wl,-z,noexecstack'])

    if conf.env.CORE == 'true':
        conf.load('qt4')

        conf.check_cxx(use = ['QTCORE'], header_name = 'QtCore/QtCore')
        conf.check_cxx(use = ['QTCORE'], header_name = 'QtCore/QtCore', type_name = 'QAtomicInt')

        conf.check_cc(lib = 'swscale')
        conf.check_cc(use = ['SWSCALE'], header_name = 'libswscale/swscale.h')
        conf.check_cc(use = ['SWSCALE'], header_name = 'libswscale/swscale.h', function_name = 'swscale_license')

        conf.check_cc(lib = 'avutil')
        conf.check_cc(use = ['AVUTIL'], header_name = 'libavutil/avutil.h')
        conf.check_cc(use = ['AVUTIL'], header_name = 'libavutil/avutil.h', function_name = 'avutil_license')

        conf.check_cc(lib = 'avcodec')
        conf.check_cc(use = ['AVCODEC'], header_name = 'libavcodec/avcodec.h')
        conf.check_cc(use = ['AVCODEC'], header_name = 'libavcodec/avcodec.h', function_name = 'avcodec_license')

    if conf.env.SCRIPT == 'true':
        conf.load('python')

        conf.check_python_version((3, 0, 0))
        conf.check_python_headers()

    if 'true' in [conf.env.CORE, conf.env.SCRIPT]:
        libs = '-lm '

        if not conf.env.DEST_OS in ['darwin', 'freebsd', 'netbsd', 'openbsd']:
            libs += '-ldl '

        conf.env.LIBS = libs.strip()

    if conf.env.FILTERS == 'true':
        conf.check_cc(lib = 'ass', mandatory = False)
        conf.check_cc(use = ['ASS'], header_name = 'ass/ass.h', mandatory = False)
        conf.check_cc(use = ['ASS'], header_name = 'ass/ass.h', function_name = 'ass_library_init', mandatory = False)

def build(bld):
    def search_paths(paths):
        srcpaths = []

        for path in paths:
            srcpaths += [os.path.join(path, '*.c'),
                         os.path.join(path, '*.cpp')]

            if bld.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
                srcpaths += [os.path.join(path, '*.asm')]
            else:
                srcpaths += [os.path.join(path, '*.S')]

        return srcpaths

    if bld.env.CORE == 'true':
        sources = search_paths([os.path.join('src', 'core'),
                                os.path.join('src', 'core', 'asm'),
                                os.path.join('src', 'core', 'asm', 'x86'),
                                os.path.join('src', 'core', 'asm', 'arm'),
                                os.path.join('src', 'core', 'asm', 'ppc')])

        if bld.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin'] and bld.env.AVISYNTH == 'true':
            sources += search_paths([os.path.join('src', 'avisynth')])

        bld(features = 'c qxx asm',
            includes = 'include',
            use = ['QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
            source = bld.path.ant_glob(sources),
            target = 'objs')

        if bld.env.SHARED == 'true':
            bld(features = 'c qxx asm cxxshlib',
                use = ['objs'],
                target = 'vapoursynth',
                install_path = '${LIBDIR}')

        if bld.env.STATIC == 'true':
            bld(features = 'c qxx asm cxxstlib',
                use = ['objs', 'QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
                target = 'vapoursynth',
                install_path = '${LIBDIR}')

    if bld.env.SCRIPT == 'true':
        script_sources = search_paths([os.path.join('src', 'vsscript')])

        bld(features = 'c qxx asm pyembed',
            includes = 'include',
            use = ['QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
            source = bld.path.ant_glob(script_sources),
            target = 'script_objs')

        if bld.env.SHARED == 'true':
            bld(features = 'c qxx asm cxxshlib pyembed',
                use = ['script_objs'],
                target = 'vapoursynth-script',
                install_path = '${LIBDIR}')

        if bld.env.STATIC == 'true':
            bld(features = 'c qxx asm cxxstlib pyembed',
                use = ['script_objs', 'QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
                target = 'vapoursynth-script',
                install_path = '${LIBDIR}')

    if bld.env.PIPE == 'true':
        pipe_sources = search_paths([os.path.join('src', 'vspipe')])

        bld(features = 'c qxx asm',
            includes = 'include',
            use = ['QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
            source = bld.path.ant_glob(pipe_sources),
            target = 'pipe_objs')

        bld(features = 'c qxx asm cxxprogram',
            includes = 'include',
            use = ['pipe_objs', 'vapoursynth', 'vapoursynth-script', 'QTCORE', 'SWSCALE', 'AVUTIL', 'AVCODEC'],
            target = 'vspipe')

    if bld.env.FILTERS == 'true':
        bld(features = 'c qxx asm cxxshlib',
            includes = 'include',
            source = bld.path.ant_glob(search_paths([os.path.join('src', 'filters', 'eedi3')])),
            target = 'eedi3',
            install_path = '${PLUGINDIR}')

        bld(features = 'c qxx asm cxxshlib',
            includes = 'include',
            source = bld.path.ant_glob(search_paths([os.path.join('src', 'filters', 'vivtc')])),
            target = 'vivtc',
            install_path = '${PLUGINDIR}')

        if bld.env.LIB_ASS:
            bld(features = 'c cxxshlib',
                includes = 'include',
                use = ['ASS'],
                source = bld.path.ant_glob(search_paths([os.path.join('src', 'filters', 'assvapour')])),
                target = 'assvapour',
                install_path = '${PLUGINDIR}')

    if bld.env.DOCS == 'true':
        bld(features = 'docs',
            source = bld.path.ant_glob([os.path.join('doc', '*.rst'),
                                        os.path.join('doc', '**', '*.rst')]),
            install_path = '${DOCDIR}')

    if bld.env.EXAMPLES == 'true':
        bld(features = 'c cxxshlib',
            includes = 'include',
            source = os.path.join('sdk', 'filter_skeleton.c'),
            target = 'example_skeleton',
            install_path = '${PLUGINDIR}')

        bld(features = 'c cxxshlib',
            includes = 'include',
            source = os.path.join('sdk', 'invert_example.c'),
            target = 'example_invert',
            install_path = '${PLUGINDIR}')

        bld.install_files('${DOCDIR}/examples',
                          bld.path.ant_glob([os.path.join('sdk', '*')]))

    if bld.env.CORE == 'true':
        bld.install_files('${INCLUDEDIR}', [os.path.join('include', 'VapourSynth.h'),
                                            os.path.join('include', 'VSHelper.h')])

        bld(source = os.path.join('pc', 'vapoursynth.pc.in'),
            install_path = '${LIBDIR}/pkgconfig',
            PREFIX = bld.env.PREFIX,
            LIBDIR = bld.env.LIBDIR,
            INCLUDEDIR = bld.env.INCLUDEDIR,
            LIBS = bld.env.LIBS,
            VERSION = VERSION)

    if bld.env.SCRIPT == 'true':
        bld.install_files('${INCLUDEDIR}', [os.path.join('include', 'VSScript.h')])

        bld(source = os.path.join('pc', 'vapoursynth-script.pc.in'),
            install_path = '${LIBDIR}/pkgconfig',
            PREFIX = bld.env.PREFIX,
            LIBDIR = bld.env.LIBDIR,
            INCLUDEDIR = bld.env.INCLUDEDIR,
            LIBS = bld.env.LIBS,
            VERSION = VERSION)

def test(ctx):
    '''runs the Cython tests'''

    for name in glob.glob(os.path.join('test', '*.py')):
        if subprocess.Popen([ctx.env.PYTHON[0], name]).wait() != 0:
            ctx.fatal('Test {0} failed'.format(name))

class TestContext(Build.BuildContext):
    cmd = 'test'
    fun = 'test'
