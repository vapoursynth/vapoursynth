LoadAllPlugins
==============

.. function::   LoadAllPlugins(string path)
   :module: std

   Loads all native VapourSynth plugins found in the
   specified *path*. Plugins that fail to load are
   silently skipped.

   Beware of Python's escape character, this will fail::

      LoadPlugin(path='c:\plugins')

   Correct ways::
   
      LoadPlugin(path='c:/plugins')
      LoadPlugin(path=r'c:\plugins')
      LoadPlugin(path='c:\\plugins\\')
