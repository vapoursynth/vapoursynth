Plugins and Scripts
===================

If you're looking for plugins and scripts then one of the most complete lists
available can be found at `vsdb.top <http://vsdb.top/>`_.

Installing with VSRepo
######################

On windows you can use the included vsrepo.py to install and upgrade plugins.
Simply run ``vsrepo.py install <namespace or identifier>``. If you need a list
of known scipts and plugins you can run ``vsrepo.py available`` or visit
`vsdb.top <http://vsdb.top/>`_.


Autoloading
###########

VapourSynth automatically loads all the native plugins located in certain
folders. Autoloading works just like manual loading, with the exception
that any errors encountered while loading a plugin are silently ignored.

.. note::

   Avoid autoloading from folders that other applications might also
   use, such as /usr/lib or /usr/local/lib in a Linux system. Several
   users reported crashes when VapourSynth attempted to load some
   random libraries (\*cough\*wxgtk\*cough\*).


Windows
*******

Windows has in total 3 different autoloading directories: user plugins, core plugins and global plugins. They are searched in that order.
User plugins are always loaded first so that the current user can always decide which exact version of a plugin is used. Core plugins follow.
Global plugins are placed last to prevent them from overriding any of the included plugins by accident.

The searched paths are:

#. *<AppData>*\\VapourSynth\\plugins32 or *<AppData>*\\VapourSynth\\plugins64
#. *<VapourSynth path>*\\core32\\plugins or *<VapourSynth path>*\\core64\\plugins
#. *<VapourSynth path>*\\plugins32 or *<VapourSynth path>*\\plugins64

Note that the per user path is not created by default. 
On modern Windows versions the *AppData* directory is located in *<user>*\\AppData\\Roaming by default.

Shortcuts to the global autoload directory are located in the start menu.

Avisynth plugins are never autoloaded. Support for this may be added in the future.

User plugins should never be put into the *core\\plugins* directory.

Windows Portable
****************

The searched paths are:

#. *<VapourSynth.dll path>*\\vapoursynth32\\coreplugins or *<VapourSynth.dll path>*\\vapoursynth64\\coreplugins
#. *<VapourSynth.dll path>*\\vapoursynth32\\plugins or *<VapourSynth.dll path>*\\vapoursynth64\\plugins

User plugins should never be put into the *coreplugins* directory.

Linux
*****

Autoloading can be configured using the file
$XDG_CONFIG_HOME/vapoursynth/vapoursynth.conf,
or $HOME/.config/vapoursynth/vapoursynth.conf if XDG_CONFIG_HOME is not
defined.

Two configuration options may be used: **UserPluginDir**, empty by default,
and **SystemPluginDir**, whose default value is set at compile time to
``$libdir/vapoursynth``, or to the location passed to the ``--with-plugindir``
argument to ``configure``.

UserPluginDir is tried first, then SystemPluginDir.

Example vapoursynth.conf::

   UserPluginDir=/home/asdf/vapoursynth/plugins
   SystemPluginDir=/special/non/default/location


OS X
****

Autoloading can be configured using the file
$HOME/Library/Application Support/VapourSynth/vapoursynth.conf. Everything else is
the same as in Linux.
