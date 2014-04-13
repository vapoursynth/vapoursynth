Plugin Autoloading
==================

VapourSynth automatically loads all the native plugins located in certain
folders. Autoloading works just like manual loading, with the exception
that any errors encountered while loading a plugin are silently ignored.


Windows
#######

TODO

Avisynth plugins are never autoloaded.


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
