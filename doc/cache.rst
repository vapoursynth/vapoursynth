Cache
=======

.. function::   Cache(clip clip[, int size, bint fixed=0])
   :module: std
   
   Inserts a Cache. Normal users should never need to use this filter as caches are automatically inserted and adapts their size according to access pattern and memory restrictions.
   
   The tweakable options *size* controls the maximum number of frames allowed to be cached at the start and *fixed* whether or not the size automatically adjusts itself.