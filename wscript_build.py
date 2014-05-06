import os

def build(ctx):
    def _search_paths(paths):
        srcpaths = []

        for path in paths:
            srcpaths += [os.path.join(path, '*.c'), os.path.join(path, '*.cpp')]

            if ctx.dependency_satisfied('cpu-x86'):
                srcpaths += [os.path.join(path, '*.asm')]
            else:
                srcpaths += [os.path.join(path, '*.S')]

        return srcpaths

    def _build_core():
        sources = _search_paths([os.path.join('src', 'core'),
                                os.path.join('src', 'core', 'asm'),
                                os.path.join('src', 'core', 'asm', 'x86'),
                                os.path.join('src', 'core', 'asm', 'arm'),
                                os.path.join('src', 'core', 'asm', 'ppc')])

        if ctx.dependency_satisfied('avisynth-compat'):
            sources += _search_paths([os.path.join('src', 'avisynth')])

        ctx(features = 'c cxx asm',
            includes = 'include',
            source = ctx.path.ant_glob(sources),
            target = 'core_objs')

        uses = ['core_objs'] + \
            ctx.dependencies_use('core') + ctx.dependencies_global()

        if ctx.dependency_satisfied('shared'):
            ctx(features = 'c cxx asm cxxshlib',
                use = uses,
                target = 'vapoursynth',
                install_path = ctx.env.LIBDIR)

        if ctx.dependency_satisfied('static'):
            ctx(features = 'c cxx asm cxxstlib',
                use = uses,
                target = 'vapoursynth',
                install_path = ctx.env.LIBDIR)

        ctx.install_files(ctx.env.INCLUDEDIR,
                          [os.path.join('include', 'VapourSynth.h'),
                           os.path.join('include', 'VSHelper.h')])

        libs = ['-l' + l for l in ctx.dependencies_libs('vapoursynth')]

        ctx(source = os.path.join('pc', 'vapoursynth.pc.in'),
            install_path = os.path.join(ctx.env.LIBDIR, 'pkgconfig'),
            PREFIX = ctx.env.PREFIX,
            LIBDIR = ctx.env.LIBDIR,
            INCLUDEDIR = ctx.env.INCLUDEDIR,
            LIBS = ' '.join(libs),
            VERSION = ctx.env.VSVERSION)

    def _build_vsscript():
        sources = _search_paths([os.path.join('src', 'vsscript')])

        ctx(features = 'c cxx asm pyembed',
            includes = 'include',
            source = ctx.path.ant_glob(sources),
            target = 'vsscript_objs')

        uses = ['vsscript_objs'] + \
            ctx.dependencies_use('vsscript') + ctx.dependencies_global()

        if ctx.dependency_satisfied('shared'):
            ctx(features = 'c cxx asm cxxshlib pyembed',
                use = uses,
                target = 'vapoursynth-script',
                install_path = ctx.env.LIBDIR)

        if ctx.dependency_satisfied('static'):
            ctx(features = 'c cxx asm cxxstlib pyembed',
                use = uses,
                target = 'vapoursynth-script',
                install_path = ctx.env.LIBDIR)

        libs = ['-l' + l for l in ctx.dependencies_libs('vapoursynth-script')]

        ctx(source = os.path.join('pc', 'vapoursynth-script.pc.in'),
            install_path = os.path.join(ctx.env.LIBDIR, 'pkgconfig'),
            PREFIX = ctx.env.PREFIX,
            LIBDIR = ctx.env.LIBDIR,
            INCLUDEDIR = ctx.env.INCLUDEDIR,
            LIBS = ' '.join(libs),
            VERSION = ctx.env.VSVERSION)

    def _build_vspipe():
        sources = _search_paths([os.path.join('src', 'vspipe')])

        ctx(features = 'c cxx asm cxxprogram',
            includes = 'include',
            source = ctx.path.ant_glob(sources),
            use =  ctx.dependencies_use('vspipe') + ctx.dependencies_global(),
            target = 'vspipe',
            install_path = ctx.env.BINDIR)

    def _build_filters():
        fpath = os.path.join('src', 'filters')

        for f in ctx.group_dependencies('filters'):
            sources = _search_paths([os.path.join(fpath, f)])

            ctx(features = 'c cxx asm cxxshlib',
                includes = ['include'] + ctx.dependencies_includes(f),
                use = ctx.dependencies_use(f) + ctx.dependencies_global(),
                source = ctx.path.ant_glob(sources),
                target = f,
                install_path = ctx.env.PLUGINDIR)

    def _build_docs():
        ctx(features = 'sphinxhtml',
            source = os.path.join('doc', 'conf.py'),
            install_path = ctx.env.DOCDIR)

    if ctx.dependency_satisfied('vapoursynth'):
        _build_core()

    if ctx.dependency_satisfied('vapoursynth-script'):
        _build_vsscript()

    if ctx.dependency_satisfied('vspipe'):
        _build_vspipe()

    _build_filters()

    if ctx.dependency_satisfied('docs'):
        _build_docs()
