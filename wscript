#!/usr/bin/env python

import os
from waflib import Utils

APPNAME = 'VapourSynth'
VERSION = '1.0' # May want to change this.

TOP = os.curdir
OUT = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.load('compiler_cxx')
    opt.load('qt4')

    opt.add_option('--mode', action = 'store', default = 'debug', help = 'the mode to compile in (debug/release)')

def configure(conf):
    def add_options(flags, options):
        for option in options:
            if option not in conf.env[flags]:
                conf.env.append_value(flags, option)

    conf.load('compiler_c')
    conf.load('compiler_cxx')
    conf.load('qt4')

    # Load Yasm explicitly, then the Nasm module which
    # supports both Nasm and Yasm.
    conf.find_program('yasm', var = 'AS', mandatory = True)
    conf.load('nasm')

    if conf.env.CXX_NAME == 'gcc':
        add_options('CXXFLAGS',
                    ['-DVSCORE_EXPORTS',
                     '-Wno-format-security'])
    elif conf.env.CXX_NAME == 'msvc':
        add_options('CXXFLAGS',
                    ['/DVSCORE_EXPORTS',
                     '/EHsc',
                     '/Zc:wchar_t-'])

    add_options('ASFLAGS',
                ['-w',
                 '-Worphan-labels',
                 '-Wunrecognized-char'])

    if conf.env.DEST_CPU in ['x86_64', 'amd64', 'x64']:
        add_options('ASFLAGS',
                    ['-DARCH_X86_64=1'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho64'
        elif conf.env.DEST_OS == 'win32':
            fmt = 'win64'
        else:
            fmt = 'elf64'
    else:
        add_options('ASFLAGS',
                    ['-DARCH_X86_64=0'])

        if conf.env.DEST_OS == 'darwin':
            fmt = 'macho32'
        elif conf.env.DEST_OS == 'win32':
            fmt = 'win32'
        else:
            fmt = 'elf32'

    add_options('ASFLAGS',
                ['-f{0}'.format(fmt)])

    if conf.options.mode == 'debug':
        if conf.env.CXX_NAME == 'gcc':
            add_options('CXXFLAGS',
                        ['-DVSCORE_DEBUG',
                         '-g',
                         '-ggdb',
                         '-ftrapv'])
        elif conf.env.CXX_NAME == 'msvc':
            add_options('CXXFLAGS',
                        ['/DVSCORE_DEBUG',
                         '/Z7'])

        add_options('ASFLAGS',
                    ['-DVSCORE_DEBUG'])
    elif conf.options.mode == 'release':
        if conf.env.CXX_NAME == 'gcc':
            add_options('CXXFLAGS',
                        ['-O3'])
        elif conf.env.CXX_NAME == 'msvc':
            add_options('CXXFLAGS',
                        ['/Ox'])
    else:
        conf.fatal('--mode must be either debug or release.')

    if not conf.env.LIB_AVUTIL:
        conf.env.LIB_AVUTIL = ['avutil']

    if not conf.env.LIB_SWSCALE:
        conf.env.LIB_SWSCALE = ['swscale']

def build(bld):
    def search_paths(paths):
        for path in paths:
            return [os.path.join(path, '*.c'),
                    os.path.join(path, '*.cpp'),
                    os.path.join(path, '*.asm')]

    sources = search_paths([os.path.join('src', 'core')])

    if bld.env.DEST_OS == 'win32':
        sources += search_paths([os.path.join('src', 'avisynth')])

    bld(features = 'qt4 c cxx asm cxxshlib',
        includes = 'include',
        source = bld.path.ant_glob(sources),
        use = ['QTCORE', 'AVUTIL', 'SWSCALE'],
        target = 'vapoursynth')
