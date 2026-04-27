import argparse
import sys

from ._utils import (
    get_include,
    get_pkgconfig_dir,
    get_plugin_dir,
    get_vsscript,
    register_install,
    register_legacy_install,
    register_vfw,
    vapoursynth_check_env,
    vapoursynth_config,
)


def _open_plugin_dir() -> None:
    import subprocess

    subprocess.run(["explorer.exe", get_plugin_dir()])


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="vapoursynth",
        description="VapourSynth command line utility.",
        epilog="Examples:\n  vapoursynth config\n  vapoursynth check-env\n  vapoursynth get-plugin-dir",
        formatter_class=lambda prog: argparse.RawDescriptionHelpFormatter(prog, max_help_position=32),
    )
    subparsers = parser.add_subparsers(title="commands", dest="command", metavar="COMMAND", required=False)

    config_parser = subparsers.add_parser(
        "config",
        help="Write or update the VapourSynth runtime configuration",
        description="Write or update the VapourSynth runtime configuration file.",
    )
    config_parser.set_defaults(func=lambda *_: vapoursynth_config())

    check_env_parser = subparsers.add_parser(
        "check-env",
        help="Show the current VapourSynth environment status",
        description="Show the current VapourSynth environment status.",
    )
    check_env_parser.set_defaults(func=lambda *_: vapoursynth_check_env())

    get_vsscript_parser = subparsers.add_parser(
        "get-vsscript",
        help="Print the bundled VSScript library path",
        description="Print the bundled VSScript library path.",
    )
    get_vsscript_parser.set_defaults(func=lambda *_: print(get_vsscript()))

    get_include_parser = subparsers.add_parser(
        "get-include",
        help="Print the bundled include directory",
        description="Print the bundled include directory.",
    )
    get_include_parser.set_defaults(func=lambda *_: print(get_include()))

    get_pkgconfig_dir_parser = subparsers.add_parser(
        "get-pkgconfig-dir",
        help="Print the bundled pkgconfig directory",
        description="Print the bundled pkgconfig directory.",
    )
    get_pkgconfig_dir_parser.set_defaults(func=lambda *_: print(get_pkgconfig_dir()))

    get_plugin_dir_parser = subparsers.add_parser(
        "get-plugin-dir",
        help="Print the bundled plugin directory",
        description="Print the bundled plugin directory.",
    )
    get_plugin_dir_parser.set_defaults(func=lambda *_: print(get_plugin_dir()))

    if sys.platform == "win32":
        open_plugin_dir_parser = subparsers.add_parser(
            "open-plugin-dir",
            help="Open the bundled plugin directory in Explorer",
            description="Open the bundled plugin directory in Explorer.",
        )
        open_plugin_dir_parser.set_defaults(func=lambda *_: _open_plugin_dir())

        register_install_parser = subparsers.add_parser(
            "register-install",
            help="Register the current installation in the user environment",
            description="Register the current installation in the user environment.",
        )
        register_install_parser.set_defaults(func=lambda *_: register_install())

        register_legacy_install_parser = subparsers.add_parser(
            "register-legacy-install",
            help="Write legacy installation registry entries",
            description="Write legacy installation registry entries.",
        )
        register_legacy_install_parser.set_defaults(func=lambda *_: register_legacy_install())

        register_vfw_parser = subparsers.add_parser(
            "register-vfw",
            help="Register the VapourSynth VFW provider",
            description="Register the VapourSynth VFW provider.",
        )
        register_vfw_parser.set_defaults(func=lambda *_: register_vfw())

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command is None:
        parser.print_help()
        return 0

    args.func(args)
    return 0
