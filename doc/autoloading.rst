Plugin Autoloading
==================

VapourSynth automatically loads all the native plugins located in certain
folders. Autoloading works just like manual loading, with the exception
that any errors encountered while loading a plugin are silently ignored.


Windows
#######

Windows has in total 3 different autoloading directories and they are searched in the order of user plugins, core plugins and global plugins.
User plugins are always loaded first so that the current user always can decide which exact version of a plugin is used. Core plugins follow
global plugins are placed last to prevent them from overriding any of the included plugins by accident.

The searched paths are:

#. *<AppData>*\\VapourSynth\\plugins32 or *<AppData>*\\VapourSynth\\plugins64
#. *<VapourSynth path>*\\core32\\plugins or *<VapourSynth path>*\\core64\\plugins
#. *<VapourSynth path>*\\plugins32 or *<VapourSynth path>*\\plugins64

Note that the per user path is not created by default. Shortcuts to the global autoload directory are also located in the start menu.
On modern windows versions the *AppData* directory is located in *<user>*\\AppData\\Roaming by default.

Avisynth plugins are never autoloaded. Support for this may be added in the future.


Linux
#####

Autoloading can be configured using the file
$XDG_CONFIG_HOME/vapoursynth/vapoursynth.conf,
or $HOME/.config/vapoursynth/vapoursynth.conf if XDG_CONFIG_HOME is not
defined.

Two configuration options may be used: **UserPluginDir**, empty by default,
and **SystemPluginDir**, whose default value is set at compile time by passing
the ``--plugin-dir`` argument to ``waf configure``.

UserPluginDir is tried first, then SystemPluginDir.

Example vapoursynth.conf::

   UserPluginDir=/home/asdf/vapoursynth/plugins
   SystemPluginDir=/special/non/default/location


OS X
####

Autoloading can be configured using the file
$HOME/Library/Application Support/VapourSynth/vapoursynth.conf. Everything else is
the same as in Linux.
