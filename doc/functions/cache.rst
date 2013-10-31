Cache
=====

.. function::   Cache(clip clip[, int size, bint fixed=0])
   :module: std

   Inserts a Cache. Normal users should never need to use this filter as caches
   are automatically inserted and adapt their size according to access patterns
   and memory restrictions.

   The tweakable option *size* controls the maximum number of frames allowed to
   be cached at the start, and *fixed* controls whether the size is
   automatically adjusted.
