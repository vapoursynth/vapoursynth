# encoding: utf-8

import os
from waflib import Task, TaskGen, Errors, Utils
from sphinx.application import Sphinx

class SphinxHTML(Task.Task):
    color = 'PINK'

    def run(self):
        self.sphinx_instance.build(force_all = False, filenames = None)
        return None

    def __str__(self):
        s = self.sphinx_instance
        return 'html: {0} -> {1}\n'.format(self.src.nice_path(), self.outdir.nice_path())

def _get_sphinx_html_outfiles(s, srcdir):
    outnode = srcdir.parent.get_bld()

    def get_out_node(path):
        outf = os.path.relpath(path, outnode.abspath())
        return outnode.make_node(outf)

    msg, dummy, iterator = s.env.update(s.config, s.srcdir, s.doctreedir, s)
    docs = []

    for docname in s.builder.status_iterator(iterator, 'reading docs...'):
        filename = docname + s.config.source_suffix
        docs.append(srcdir.find_node(filename))

    for dep in s.env.dependencies.values():
        for d in dep:
            docs.append(srcdir.find_node(d.encode()))

    outfs = []
    for doc in docs:
        outf = os.path.relpath(doc.abspath(), srcdir.abspath())
        outf = os.path.splitext(outf)[0]
        outf = s.builder.get_outfilename(outf)
        outf = os.path.relpath(outf, outnode.abspath())
        outf = outnode.make_node(outf)
        outfs.append(outf)

    if s.builder.config.html_use_index:
        outfs.append(get_out_node(s.builder.get_outfilename('genindex')))

    outfs.append(get_out_node(s.builder.get_outfilename('search')))
    outfs.append(get_out_node(os.path.join(s.builder.outdir,
                                           s.builder.searchindex_filename)))

    return outfs

def _get_sphinx_html_staticfiles(s, outnode):
    def get_static_node(odir, rpath, fname):
        sfile = os.path.normpath(os.path.join(odir, rpath, fname))
        sfile = os.path.relpath(sfile, outnode.abspath())
        return outnode.make_node(sfile)

    sfiles = []
    hpath = os.path.join(outnode.abspath(), 'html')

    themedirs = [(os.path.join(t, 'static'), os.path.join(hpath, '_static')) \
                 for t in s.builder.theme.get_dirchain()]
    customdirs = [(os.path.join(s.builder.confdir, d), os.path.join(hpath, d)) \
                  for d in s.config.html_static_path]

    for sdir, odir in themedirs + customdirs:
        for d in os.walk(sdir):
            rpath = os.path.relpath(d[0], sdir)
            for f in d[2]:
                fname = f
                if fname.endswith('_t'):
                    fname = fname[:-2]

                sfiles.append(get_static_node(odir, rpath, fname))

    bout = s.builder.outdir

    n = get_static_node(bout, '_static', 'pygments.css')
    sfiles.append(n)
    if s.builder.config.language is not None:
        n = get_static_node(bout, '_static', 'translations.js')
        sfiles.append(n)
    if s.builder.config.html_logo:
        n = get_static_node(bout, '_static', \
                            os.path.basename(s.builder.config.html_logo))
        sfiles.append(n)
    if s.builder.config.html_favicon:
        n = get_static_node(bout, '_static', \
                            os.path.basename(s.builder.config.html_favicon))
        sfiles.append(n)
    if s.builder.config.html_use_opensearch:
        n = get_static_node(bout, '_static', 'opensearch.xml')
        sfiles.append(n)

    return sfiles

@TaskGen.feature('sphinxhtml')
@TaskGen.before_method('process_source')
def apply_sphinx(tg):
    conf = tg.path.find_node(tg.source)
    confdir = conf.parent.abspath()
    outnode = conf.parent.get_bld()
    outdir = getattr(tg, 'outdir', os.path.join(outnode.abspath(), 'html'))
    doctreedir = getattr(tg, 'doctreedir', os.path.join(outdir, '.doctrees'))

    s = Sphinx(confdir, confdir, outdir, doctreedir, 'html', status = None)

    sources = _get_sphinx_html_outfiles(s, conf.parent)
    sources += _get_sphinx_html_staticfiles(s, outnode)

    inst_to = getattr(tg, 'install_path')
    chmod = getattr(tg, 'chmod', Utils.O644)

    if inst_to:
        i = 0
        for src in sources:
            path = src.parent.path_from(outnode)
            path = os.path.normpath(os.path.join(inst_to, path))

            setattr(tg, 'install_task_{0}'.format(i),
                    tg.bld.install_files(path, src,
                                         env = tg.env, chmod = chmod))

            i += 1

    task = tg.create_task('SphinxHTML', src = conf, tgt = sources)
    task.src = conf
    task.srcdir = tg.bld.root.find_node(s.srcdir)
    task.outdir = tg.bld.root.find_node(s.outdir)
    task.sphinx_instance = s

    tg.source = []
