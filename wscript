#!/usr/bin/env python

import os
from waflib import Utils

APPNAME = 'VapourSynth'
VERSION = '8'

TOP = os.curdir
OUT = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.load('compiler_cxx')
    opt.load('qt4')

    opt.add_option('--mode', action = 'store', default = 'debug', help = 'the mode to compile in (debug/release)')
    opt.add_option('--static', action = 'store', default = 'false', help = 'build a static library (true/false)')

def configure(conf):
    def add_options(flags, options):
        for flag in flags:
            for option in options:
                if option not in conf.env[flag]:
                    conf.env.append_value(flag, option)

    conf.load('compiler_c')
    conf.load('compiler_cxx')
    conf.load('qt4')

    # Load Yasm explicitly, then the Nasm module which
    # supports both Nasm and Yasm.
    conf.find_program('yasm', var = 'AS', mandatory = True)
    conf.load('nasm')

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
                 '-Wunrecognized-char'])

    if conf.env.DEST_CPU in ['x86_64', 'amd64', 'x64']:
        add_options(['ASFLAGS'],
                    ['-DARCH_X86_64=1'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho64'
        elif conf.env.DEST_OS == 'win32':
            fmt = 'win64'
        else:
            fmt = 'elf64'
    else:
        add_options(['ASFLAGS'],
                    ['-DARCH_X86_64=0'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho32'
        elif conf.env.DEST_OS == 'win32':
            fmt = 'win32'
        else:
            fmt = 'elf32'

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
        add_options(['LINKFLAGS_cxxshlib', 'LINKFLAGS_cxxprogram'],
                    ['-Wl,-Bsymbolic'])

    conf.env.STATIC = conf.options.static

    if not conf.env.STATIC in ['true', 'false']:
        conf.fatal('--static must be either true or false.')

    conf.check_cxx(lib = 'QtCore', features = 'cxx cxxprogram')
    conf.check_cxx(lib = 'avutil', features = 'cxx cxxprogram')
    conf.check_cxx(lib = 'swscale', features = 'cxx cxxprogram')

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

    if bld.env.DEST_OS == 'win32':
        sources += search_paths([os.path.join('src', 'avisynth')])

    bld(features = 'c qxx asm',
        includes = 'include',
        use = ['QTCORE', 'AVUTIL', 'SWSCALE'],
        source = bld.path.ant_glob(sources),
        target = 'objs')

    bld(features = 'c qxx asm cxxshlib',
        use = ['objs'],
        target = 'vapoursynth')

    if bld.env.STATIC == 'true':
        bld(features = 'c qxx asm cxxstlib',
            use = ['objs', 'QTCORE', 'AVUTIL', 'SWSCALE'],
            target = 'vapoursynth')
