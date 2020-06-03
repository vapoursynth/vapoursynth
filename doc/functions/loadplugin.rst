LoadPlugin
==========

.. function::   LoadPlugin(string path, bint altsearchpath = False)
   :module: std

   Load a native VapourSynth plugin. If successful, the loaded plugin's
   functions will end up in their own namespace.

   Returns an error if a plugin with the same identifier or namespace already
   has been loaded. This is to prevent naming collisions or multiple versions
   of the same plugin being loaded at once.
   
   Plugins are normally loaded with a very specific search order for
   dependencies. Setting *altsearchpath* modifies this behavior to also
   include dlls in the PATH.

   Beware of Python's escape character, this will fail::

      LoadPlugin(path='c:\plugins\filter.dll')

   Correct ways::
   
      LoadPlugin(path='c:/plugins/filter.dll')
      LoadPlugin(path=r'c:\plugins\filter.dll')
      LoadPlugin(path='c:\\plugins\\filter.dll')
