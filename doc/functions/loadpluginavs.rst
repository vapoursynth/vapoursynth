LoadPlugin (Avisynth Compatibility)
===================================

.. function::   LoadPlugin(string path)
   :module: avs

   Load an Avisynth plugin. If successful the loaded plugin's functions will end
   up in the avs namespace.

   The compatibility module can work with a large number of Avisynth's plugins.
   However, the wrapping is not complete, so the following things will cause
   problems:

      * The plugin tries to call env->invoke().
        These calls are ignored when it is safe to do so, but otherwise they
        will most likely trigger a fatal error.
      * Plugins trying to read global variables.
        There are no global variables.
      * Plugins that use packed RGB24 input/output.
        Nobody used it anyway.
      * May deadlock when the Avisynth compatibility module does not have a
        prefetch list.
        Can be worked around by increasing the number of threads used if it
        deadlocks.

   Returns an error if there are function name collisions.

   Watch out for escape characters::

      # Causes an error because \ is Python's escape character
      LoadPlugin(path='c:\plugins\filter.dll')
      # The correct way(s)
      LoadPlugin(path='c:/plugins/filter.dll')
      LoadPlugin(path=r'c:\plugins\filter.dll')
      LoadPlugin(path='c:\\plugins\\filter.dll')
