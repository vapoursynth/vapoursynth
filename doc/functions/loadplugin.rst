LoadPlugin
==========

.. function::   LoadPlugin(string path)
   :module: std

   Load a native VapourSynth plugin. If successful, the loaded plugin's
   functions will end up in their own namespace.

   Returns an error if a plugin with the same identifier or namespace already
   has been loaded. This is to prevent naming collisions or multiple versions
   of the same plugin being loaded at once.

   Beware of Python's escape character, this will fail::

      LoadPlugin(path='c:\plugins\filter.dll')

   Correct ways::
   
      LoadPlugin(path='c:/plugins/filter.dll')
      LoadPlugin(path=r'c:\plugins\filter.dll')
      LoadPlugin(path='c:\\plugins\\filter.dll')
