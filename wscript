import glob, os, subprocess, sys
from waflib import Build, Task, TaskGen, Utils

APPNAME = 'VapourSynth'
VERSION = '17'

TOP = os.curdir
OUT = 'build'

class preproc(Task.Task):
    "Preprocess Cython source files"

    ext_out = ['.pyx']
    inst_to = None
    color = 'CYAN'

    def run(self):
        if self.env.CXX_NAME == 'gcc':
            params = ['-E', '-x', 'c']
        elif self.env.CXX_NAME == 'msvc':
            params = ['/nologo', '/E']

        args = [Utils.subst_vars('${CC}', self.env)] + params + [self.inputs[0].abspath()]

        with open(self.outputs[0].abspath(), 'w') as f:
            subprocess.Popen(args, stdout = f).wait()

@TaskGen.extension('.pyx')
def add_pyx_file(self, node):
    self.create_task('preproc', node, node.get_bld().change_ext('.pyx'))

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
    opt.add_option('--shared', action = 'store', default = 'true', help = 'build a shared library (true/false)')
    opt.add_option('--static', action = 'store', default = 'false', help = 'build a static library (true/false)')
    opt.add_option('--filters', action = 'store', default = 'true', help = 'build included filters (true/false)')
    opt.add_option('--cython', action = 'store', default = 'true', help = 'build Cython wrapper (true/false)')
    opt.add_option('--avisynth', action = 'store', default = 'true', help = 'build Avisynth compatibility layer (true/false)')
    opt.add_option('--docs', action = 'store', default = 'false', help = 'build the documentation (true/false)')
    opt.add_option('--examples', action = 'store', default = 'false', help = 'install SDK examples (true/false)')

def configure(conf):
    def add_options(flags, options):
        for flag in flags:
            conf.env.append_unique(flag, options)

    conf.load('compiler_c')
    conf.load('compiler_cxx')
    conf.load('qt4')

    if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64', 'amd64', 'x86_amd64']:
        # Load Yasm explicitly, then the Nasm module which
        # supports both Nasm and Yasm.
        conf.find_program('yasm', var = 'AS', mandatory = True)
        conf.load('nasm')

    conf.find_program(['python3', 'python'], var = 'PYTHON', mandatory = True)

    if conf.env.DEST_OS == 'darwin':
        if conf.env.CXX_NAME == 'gcc':
            add_options(['ASFLAGS'],
                        ['-DPREFIX'])

    if conf.env.CXX_NAME == 'gcc':
        add_options(['CFLAGS', 'CXXFLAGS'],
                    ['-DVSCORE_EXPORTS',
                     '-fPIC'])
    elif conf.env.CXX_NAME == 'msvc':
        add_options(['CFLAGS', 'CXXFLAGS'],
                    ['/DVSCORE_EXPORTS',
                     '/EHsc',
                     '/Zc:wchar_t-'])

    add_options(['ASFLAGS'],
                ['-w',
                 '-Worphan-labels',
                 '-Wunrecognized-char',
                 '-Dprogram_name=vs'])

    if conf.env.DEST_CPU in ['x86_64', 'x64', 'amd64', 'x86_amd64']:
        add_options(['ASFLAGS'],
                    ['-DARCH_X86_64=1', '-DPIC'])

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

    if conf.env.DEST_CPU in ['x86', 'x86_64', 'x64']:
        add_options(['ASFLAGS'],
                    ['-f{0}'.format(fmt)])

    if conf.options.mode == 'debug':
        if conf.env.CXX_NAME == 'gcc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['-DVSCORE_DEBUG',
                         '-g',
                         '-ggdb',
                         '-ftrapv'])
        elif conf.env.CXX_NAME == 'msvc':
            add_options(['CFLAGS', 'CXXFLAGS'],
                        ['/DVSCORE_DEBUG',
                         '/Z7'])

        add_options(['ASFLAGS'],
                    ['-DVSCORE_DEBUG'])
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
                        ['-Wl,-Bsymbolic'])

    conf.msg("Setting DEST_OS to", conf.env.DEST_OS)
    conf.msg("Setting DEST_CPU to", conf.env.DEST_CPU)
    conf.msg("Setting DEST_BINFMT to", conf.env.DEST_BINFMT)

    def check_feature(name, desc):
        val = conf.options.__dict__[name]

        if not val in ['true', 'false']:
            conf.fatal('--{0} must be either true or false.'.format(name))
        else:
            u = name.upper()

            conf.env[u] = val
            conf.define('FEATURE_' + u, 1 if val == 'true' else 0)
            conf.msg("Enabling {0}?".format(desc), 'yes' if conf.env[u] == 'true' else 'no')

    check_feature('shared', 'shared library')
    check_feature('static', 'static library')
    check_feature('filters', 'included filters')
    check_feature('cython', 'Cython wrapper')
    check_feature('avisynth', 'Avisynth compatibility')
    check_feature('docs', 'documentation')
    check_feature('examples', 'SDK examples')

    conf.define('PATH_PREFIX', conf.env.PREFIX)
    conf.msg("Setting PREFIX to", conf.env.PREFIX)

    for dir in ['libdir', 'plugindir', 'docdir', 'includedir']:
        u = dir.upper()

        conf.env[u] = Utils.subst_vars(conf.options.__dict__[dir], conf.env)
        conf.define('PATH_' + u, conf.env[u])
        conf.msg("Setting {0} to".format(u), conf.env[u])

    conf.check_cxx(use = ['QTCORE'], header_name = 'QtCore/QtCore')
    conf.check_cxx(use = ['QTCORE'], header_name = 'QtCore/QtCore', type_name = 'QAtomicInt')

    conf.check_cc(lib = 'avutil')
    conf.check_cc(use = ['AVUTIL'], header_name = 'libavutil/avutil.h')
    conf.check_cc(use = ['AVUTIL'], header_name = 'libavutil/avutil.h', function_name = 'avutil_license')

    conf.check_cc(lib = 'swscale')
    conf.check_cc(use = ['SWSCALE'], header_name = 'libswscale/swscale.h')
    conf.check_cc(use = ['SWSCALE'], header_name = 'libswscale/swscale.h', function_name = 'swscale_license')

    conf.check_cc(lib = 'ass')
    conf.check_cc(use = ['ASS'], header_name = 'ass/ass.h')
    conf.check_cc(use = ['ASS'], header_name = 'ass/ass.h', function_name = 'ass_library_init')

    libs = '-lm '

    if not conf.env.DEST_OS in ['darwin', 'freebsd', 'netbsd', 'openbsd']:
        libs += '-ldl '

    conf.env.LIBS = libs.strip()

def build(bld):
    def search_paths(paths):
        srcpaths = []

        for path in paths:
            srcpaths += [os.path.join(path, '*.c'),
                         os.path.join(path, '*.cpp'),
                         os.path.join(path, '*.asm')]

        return srcpaths

    sources = search_paths([os.path.join('src', 'core'),
                            os.path.join('src', 'core', 'asm')])

    if bld.env.DEST_OS in ['win32', 'cygwin', 'msys', 'uwin'] and bld.env.AVISYNTH == 'true':
        sources += search_paths([os.path.join('src', 'avisynth')])

    bld(features = 'c qxx asm',
        includes = 'include',
        use = ['QTCORE', 'SWSCALE', 'AVUTIL'],
        source = bld.path.ant_glob(sources),
        target = 'objs')

    if bld.env.SHARED == 'true':
        bld(features = 'c qxx asm cxxshlib',
            use = ['objs'],
            target = 'vapoursynth',
            install_path = '${LIBDIR}')

    if bld.env.STATIC == 'true':
        bld(features = 'c qxx asm cxxstlib',
            use = ['objs', 'QTCORE', 'SWSCALE', 'AVUTIL'],
            target = 'vapoursynth',
            install_path = '${LIBDIR}')

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

        bld(features = 'c cxxshlib',
            includes = 'include',
            use = ['ASS'],
            source = bld.path.ant_glob(search_paths([os.path.join('src', 'filters', 'assvapour')])),
            target = 'assvapour',
            install_path = '${PLUGINDIR}')

    if bld.env.CYTHON == 'true':
        bld(features = 'preproc',
            source = bld.path.ant_glob([os.path.join('src', 'cython', '*.pyx')]))

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
            install_path = None)

        bld(features = 'c cxxshlib',
            includes = 'include',
            source = os.path.join('sdk', 'invert_example.c'),
            target = 'example_invert',
            install_path = None)

        bld.install_files('${DOCDIR}/examples',
                          bld.path.ant_glob([os.path.join('sdk', '*')]))

    bld.install_files('${INCLUDEDIR}', [os.path.join('include', 'VapourSynth.h'),
                                        os.path.join('include', 'VSHelper.h')])

    bld(source = 'vapoursynth.pc.in',
        install_path = '${LIBDIR}/pkgconfig',
        PREFIX = bld.env.PREFIX,
        LIBDIR = bld.env.LIBDIR,
        INCLUDEDIR = bld.env.INCLUDEDIR,
        LIBS = bld.env.LIBS,
        VERSION = VERSION)

def test(ctx):
    '''runs the Cython tests'''

    for name in glob.glob(os.path.join('test', '*.py')):
        if subprocess.Popen([ctx.env.PYTHON, name]).wait() != 0:
            ctx.fatal('Test {0} failed'.format(name))

class TestContext(Build.BuildContext):
    cmd = 'test'
    fun = 'test'
