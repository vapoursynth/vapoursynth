SetMaxCPU
=========

.. function::   SetMaxCPU(string cpu)
   :module: std

   This function is only intended for testing and debugging purposes
   and sets the maximum used instruction set for optimized functions.
   
   Possible values for x86: "avx2", "sse2", "none"
   
   Other platforms: "none"
   
   By default all supported cpu features are used.