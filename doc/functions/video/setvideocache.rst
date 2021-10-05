SetVideoCache
=============

.. function:: SetVideoCache(vnode clip[, int mode, int fixedsize, int maxsize, int historysize])
   :module: std

   Every filter node has a cache associated with it that
   may or may not be enabled depending on the dependencies
   and request patterns. This function allows all automatic
   behavior to be overridden.
   
   The *mode* option has 3 possible options where 0 always
   disables caching, 1 always enables the cache and -1
   uses the automatically calculated settings. Note that
   setting *mode* to -1 will reset the other values to
   their defaults as well.
   
   The other options are fairly self-explanatory where
   setting *fixedsize* prevents the cache from over time
   altering its *maxsize* based on request history. The
   final *historysize* argument controls how many previous
   and no longer cached requests should be considered when
   adjusting *maxsize*, generally this value should not
   be touched at all.
   
   Note that setting *mode* will reset all other options
   to their defaults.