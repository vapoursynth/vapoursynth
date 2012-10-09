Python Bindings
===============
VapourSynth is separated into a core library and a Python module. This section deals with how the core library is exposed through Python and some of the special things, such as slicing and output,
are unique to Python scripting.

VapourSynth Structure and Functions
###################################
To use the VapourSynth library you must first create a Core object. This core may then load plugins which all end up in their own unit, or namespace so to say, to avoid naming conflicts in
the contained functions. For this reason you call a plugin function with *core.unit.Function()*. Note that the loaded plugins are per core instance and not global so if you do create several
processing cores at once (not recommended) you will have to load the plugins you want for each core.

All arguments to functions have names that are lowercase and all function names are CamelCase, unit names are also lowercase and usually short. This is good to remember. If you do not like
CamelCase for function names you can pass *accept_lowercase=True* to the Core constructor.

Slicing
#######
The VideoNode class (always referred to as clip in practice) supports the full range of indexing and slicing operations in Python.
If you do perform a slicing operation on a clip you will get a new clip back with the desired frames.
Here are some examples to illustrate::

   # ret will be a one frame clip containing the 6th frame
   ret = clip[5]
   # ret will contain frame 7 to 9 (unlike trim the end value of python slicing is not inclusive)
   ret = clip[6:10]
   
   # Select even numbered frames
   ret = clip[::2]
   # Select odd numbered frames
   ret = clip[1::2]
   
   # Negative step is also allowed so this reverses a clip
   ret = clip[::-1]
   
   # It may all be combined at once to confuse people just like normal Python lists
   ret = clip[-400:-800:-5]
   
Output to File
##############
The VideoNode class has a method to output all frames in a clip to a file handle. For example *sys.stdout* if you want to pipe it to another application. This illustrates the typical use::

   someclip.output(sys.stdout, y4m=False)
   
In general you should only ever have to change *y4m*, in case you want to append YUV4MPEG2 headers to the output.

Special Output
##############
If you want to open a script through VFW, VSFS or any other extension that uses the standard python interface you need to follow a few additional rules.
The clip you want to output has to be assigned to the *last* variable in the __main__ module. There are also other variables that can be set to control
how a format is output. For example setting *enable_v210=True* changes the packing of the YUV422P10 format to one that is common in professional software (like Adobe products).
An example on how to get v210 output::

   last = core.resize.Bicubic(clip, format=vs.YUV422P10)
   enable_v210 = True

Raw Access to Frame Data
########################
The VideoFrame class simply contains one picture and all the metadata associated with it. It is possible to access the raw data using ctypes and some persistence.
The two relevant functions are *get_read_ptr(plane)*, *get_write_ptr(plane)* and *get_stride(plane)*, both of which take the plane to acces as an argument. Accessing the data is a bit trickier as 
*get_data()* only returns a pointer. To get a frame simply call *get_frame(n)* on a clip.
