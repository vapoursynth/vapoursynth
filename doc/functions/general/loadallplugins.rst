LoadAllPlugins
==============

.. function::   LoadAllPlugins(string path)
   :module: std

   Loads all native VapourSynth plugins found in the
   specified *path*. Plugins that fail to load are
   silently skipped.

   Beware of Python's escape character, this will fail::

      LoadAllPlugins(path='c:\plugins')

   Correct ways::

      LoadAllPlugins(path='c:/plugins')
      LoadAllPlugins(path=r'c:\plugins')
      LoadAllPlugins(path='c:\\plugins\\')
