StackVertical/StackHorizontal
=============================

.. function:: StackVertical(clip[] clips)
              StackHorizontal(clip[] clips)
   :module: std
   
   Stacks all given *clips* together. The same format is a requirement. For stackvertical all clips also need to be the same width and for stackhorizontal all clips need to be the same height.
   If one of the clips is infinite length then the returned clip will also be that.
   