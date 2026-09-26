SetMaxCPU
=========

.. function::   SetMaxCPU(string cpu)
   :module: std

   This function is only intended for testing and debugging purposes
   and sets the maximum used instruction set for optimized functions.

   Possible values for x86: "avx512", "avx2", "sse2", "none"

   Possible values for ARM64: "neon", "none"

   Other platforms: "none"

   By default all supported cpu features are used, and an empty string
   restores that default. Any other value is an error.

   Returns the level that was set before the call, as one of the strings
   above, or an empty string if it was the default.