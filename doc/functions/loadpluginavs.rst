LoadPlugin (Avisynth Compatibility)
===================================

.. function::   LoadPlugin(string path)
   :module: avs

   Load an Avisynth 2.5 (32 bit only) or 2.6 (32 and 64 bit) plugin. If successful,
   the loaded plugin's functions will end up in the avs namespace.

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

   If there are function name collisions functions will have a number appended
   to them to make them distinct. For example if three functions are named
   *func* then they will be named *func*, *func_2* and *func_3*. This means
   that Avisynth functions that have multiple overloads (rare) will give
   each overload a different name.

   Beware of Python's escape character, this will fail::

      LoadPlugin(path='c:\plugins\filter.dll')

   Correct ways::
   
      LoadPlugin(path='c:/plugins/filter.dll')
      LoadPlugin(path=r'c:\plugins\filter.dll')
      LoadPlugin(path='c:\\plugins\\filter.dll')
