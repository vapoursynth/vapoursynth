Cache
=====

.. function::   Cache(clip clip[, int size, bint fixed=False, make_linear=False])
   :module: std

   Inserts a Cache. Users of the Python module should never need to use this
   filter, as caches are automatically inserted and adapt their size according
   to access patterns and memory restrictions.

   The tweakable option *size* controls the maximum number of frames allowed to
   be cached at the start, and *fixed* controls whether the size is
   automatically adjusted.
   
   There is also *make_linear* which will make the cache try to make requests
   more linear if at all possible. This obviously comes with a speed penalty
   so never use it unless necessary.
