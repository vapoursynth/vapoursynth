import argparse
import ctypes
import os
import sys
from ctypes.util import find_library
from pathlib import Path, PurePath

from .vapoursynth import Error, __version__


def get_include():
    """Return the directory that contains the VapourSynth header files."""
    return os.path.join(os.path.dirname(__file__), "include")


def get_plugin_dir():
    """Return the VapourSynth plugin directory location."""
    return os.path.join(os.path.dirname(__file__), "plugins")


def get_vsscript():
    """Return the location of the vsscript library."""
    if sys.platform == "win32":
        return os.path.join(os.path.dirname(__file__), "vsscript.dll")
    elif sys.platform == "darwin":
        return os.path.join(os.path.dirname(__file__), "libvsscript.4.dylib")
    else:
        return os.path.join(os.path.dirname(__file__), "libvsscript.so.4")


# All code for scripts executables


def _version_string_to_number(version_string):
    version_parts = version_string.strip().split(".", 4)
    while len(version_parts) < 4:
        version_parts.append("0")
    return (
        (int(version_parts[0]) << 48)
        | (int(version_parts[1]) << 32)
        | (int(version_parts[2]) << 16)
        | (int(version_parts[3]) << 0)
    )


def _is_msi_product_installed(upgrade_code, min_version):
    from ctypes.wintypes import DWORD, LPCWSTR, LPDWORD, LPWSTR, UINT

    msi = ctypes.WinDLL("msi.dll")
    MsiEnumRelatedProductsW = msi.MsiEnumRelatedProductsW
    MsiEnumRelatedProductsW.argtypes = [LPCWSTR, DWORD, DWORD, LPWSTR]
    MsiEnumRelatedProductsW.restype = UINT
    MsiGetProductInfoW = msi.MsiGetProductInfoW
    MsiGetProductInfoW.argtypes = [LPCWSTR, LPCWSTR, LPWSTR, LPDWORD]
    MsiGetProductInfoW.restype = UINT

    product_code_buf = ctypes.create_unicode_buffer(39)
    if MsiEnumRelatedProductsW(upgrade_code, 0, 0, product_code_buf) != 0:
        return False

    version_string_size = ctypes.wintypes.DWORD(16)
    version_string_buf = ctypes.create_unicode_buffer(version_string_size.value)

    err_code = MsiGetProductInfoW(
        product_code_buf.value, "VersionString", version_string_buf, ctypes.byref(version_string_size)
    )

    if err_code == 234:  # ERROR_MORE_DATA
        version_string_size.value = version_string_size.value + 1
        version_string_buf = ctypes.create_unicode_buffer(version_string_size.value)
        err_code = MsiGetProductInfoW(
            product_code_buf.value, "VersionString", version_string_buf, ctypes.byref(version_string_size)
        )

    if err_code != 0:
        return False

    return _version_string_to_number(version_string_buf.value) >= _version_string_to_number(min_version)


def _check_visual_studio_runtime():
    if sys.platform == "win32":
        if not _is_msi_product_installed("{36F68A90-239C-34DF-B58C-64B30153CE35}", "14.50.35719.0"):
            print("The Visual Studio 2015-2026 runtime which is required to run VapourSynth is missing or too old!")
            print("The latest version can be downloaded from:")
            print("    x64: https://aka.ms/vc14/vc_redist.x64.exe")
            print("  arm64: https://aka.ms/vc14/vc_redist.arm64.exe")


def _find_python_symbol_path():
    if sys.platform == "win32":
        from ctypes.wintypes import DWORD, HMODULE, LPWSTR, MAX_PATH

        kernel32 = ctypes.WinDLL("kernel32.dll")
        GetModuleFileNameW = kernel32.GetModuleFileNameW
        GetModuleFileNameW.argtypes = [HMODULE, LPWSTR, DWORD]
        GetModuleFileNameW.restype = DWORD
        buf = ctypes.create_unicode_buffer(MAX_PATH * 4 + 1)
        if GetModuleFileNameW(ctypes.pythonapi._handle, buf, MAX_PATH * 4) == 0:
            return None
        return buf.value
    else:

        class Dl_info(ctypes.Structure):
            _fields_ = [
                ("dli_fname", ctypes.c_char_p),
                ("dli_fbase", ctypes.c_void_p),
                ("dli_sname", ctypes.c_char_p),
                ("dli_saddr", ctypes.c_void_p),
            ]

        libdl = ctypes.CDLL(find_library("dl"))
        libdl.dladdr.argtypes = [ctypes.c_void_p, ctypes.POINTER(Dl_info)]
        libdl.dladdr.restype = ctypes.c_int

        dlinfo = Dl_info()
        retcode = libdl.dladdr(ctypes.cast(ctypes.pythonapi.Py_GetVersion, ctypes.c_void_p), ctypes.pointer(dlinfo))

        libpath = None

        if retcode != 0:
            libpath = os.path.realpath(dlinfo.dli_fname.decode())

        # Always correct on mac with system and brew python so exit early
        if sys.platform == "darwin":
            return libpath

        # We can't dlopen executables on Linux so make sure it's a proper library
        if libpath:
            try:
                pylib = ctypes.CDLL(libpath)
                if pylib and pylib.Py_GetVersion:
                    return libpath
            except Exception:
                pass

        # Now for general path guessing based on compile time values instead
        from sysconfig import get_config_var, get_config_vars

        libfilenames = []

        suffix = ".so"

        INSTSONAME = get_config_var("INSTSONAME")
        if INSTSONAME and suffix in INSTSONAME:
            libfilenames.append(INSTSONAME)

        LDLIBRARY = get_config_var("LDLIBRARY")
        if LDLIBRARY and os.path.splitext(LDLIBRARY)[1] == suffix:
            libfilenames.append(LDLIBRARY)

        LIBRARY = get_config_var("LIBRARY")
        if LIBRARY and os.path.splitext(LIBRARY)[1] == suffix:
            libfilenames.append(LIBRARY)

        libpaths = get_config_vars("LIBPL", "srcdir", "LIBDIR")
        for fn in libfilenames:
            for path in libpaths:
                if path:
                    pylib = Path(path) / fn
                    if pylib.is_file():
                        return pylib
        return None


def _check_windows_env():
    if sys.platform == "win32":
        import winreg

        vapoursynth_path = None

        try:
            key = winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"SOFTWARE\VapourSynth", 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY
            )
            try:
                vapoursynth_path = winreg.QueryValueEx(key, "Path")[0]
            finally:
                winreg.CloseKey(key)
        except:
            pass

        if vapoursynth_path == os.path.dirname(__file__):
            print("Registry entries: this installation")
        elif vapoursynth_path:
            print(f'Registry entries: (other installation) "{vapoursynth_path}"')
        else:
            print("Registry entries: not set")

        vfw_path = None

        try:
            key = winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32",
                0,
                winreg.KEY_READ | winreg.KEY_WOW64_64KEY,
            )
            try:
                vfw_path = winreg.QueryValueEx(key, None)[0]
            finally:
                winreg.CloseKey(key)
        except:
            pass

        if vfw_path == os.path.join(os.path.dirname(__file__), "vsvfw.dll"):
            print("VFW module: this installation")
        elif vfw_path:
            print(f'VFW module: (other installation) "{vfw_path}"')
        else:
            print("VFW module: not set")


def vapoursynth_check_env():
    _check_visual_studio_runtime()

    vsscript_path = os.getenv("VSSCRIPT_PATH")

    fsize = 0
    try:
        fsize = Path(__file__).with_name("vspyenv.cfg").stat().st_size
    except:
        pass
    if fsize == 0:
        print("VAPOURSYNTH IS NOT CONFIGURED! RUN VAPOURSYNTH CONFIG!")

    print(f'VapourSynth path: "{os.path.dirname(__file__)}"')

    if vsscript_path == get_vsscript():
        print("VSSCRIPT_PATH: this installation")
    elif vsscript_path:
        print(f'VSSCRIPT_PATH: (other installation) "{vsscript_path}"')
    else:
        print("VSSCRIPT_PATH: not set")

    _check_windows_env()


def vapoursynth_config():
    _check_visual_studio_runtime()

    config_path = PurePath(__file__)
    config_path = config_path.with_name("vspyenv.cfg")

    py_symbol_path = _find_python_symbol_path()
    if py_symbol_path is None:
        raise Error("Couldn't determine location of Python library!")

    with open(config_path, "w", encoding="utf-8") as f:
        f.write(f"executable = {sys.executable}\n")
        f.write(f"py-symbol-path = {py_symbol_path}\n")

    print(f"Configuration successfully written to {config_path}")


def _write_registry_entries(entries):
    import winreg

    for entry in entries:
        try:
            key = winreg.CreateKeyEx(
                winreg.HKEY_CURRENT_USER, entry["subkey"], 0, winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY
            )

            try:
                winreg.SetValueEx(key, entry["value_name"], 0, winreg.REG_SZ, str(entry["value_data"]))
            finally:
                winreg.CloseKey(key)

        except PermissionError:
            print(f"Permission denied: {entry['subkey']}")
            print("Try running as administrator or don't run in global mode")
            return False
        except Exception:
            raise

    return True


def register_legacy_install():
    if sys.platform != "win32":
        raise Error("Command is only supported on Windows!")

    entries = [
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "Version",
            "value_data": __version__,
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "Path",
            "value_data": PurePath(__file__).parent,
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "Plugins",
            "value_data": get_plugin_dir(),
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "VapourSynthDLL",
            "value_data": PurePath(__file__).with_name("libvapoursynth.dll"),
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "VSScriptDLL",
            "value_data": get_vsscript(),
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "VSPipeEXE",
            "value_data": PurePath(__file__).with_name("vspipe.exe"),
        },
        {
            "subkey": r"SOFTWARE\VapourSynth",
            "value_name": "PythonPath",
            "value_data": PurePath(sys.executable).parent,
        },
    ]

    if not _write_registry_entries(entries):
        print("Couldn't write to registry!")
        sys.exit(1)
    else:
        print("Successfully wrote legacy installation information to registry!")


def register_install():
    if sys.platform != "win32":
        raise Error("Command is only supported on Windows!")

    entries = [
        {
            "subkey": "Environment",
            "value_name": "VSSCRIPT_PATH",
            "value_data": get_vsscript(),
        }
    ]

    if not _write_registry_entries(entries):
        print("Couldn't write to registry!")
        sys.exit(1)
    else:
        from ctypes.wintypes import HWND, UINT, WPARAM

        user32 = ctypes.WinDLL("user32.dll")
        SendMessageTimeoutW = user32.SendMessageTimeoutW
        SendMessageTimeoutW.argtypes = [HWND, UINT, WPARAM, ctypes.c_wchar_p, UINT, UINT, ctypes.c_void_p]
        SendMessageTimeoutW.restype = ctypes.c_void_p
        #       SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment", SMTO_ABORTIFHUNG, 0, 2000, NULL)
        SendMessageTimeoutW(0xFFFF, 0x001A, 0, "Environment", 2, 2000, 0)
        print("Successfully set environment variables!")


def register_vfw():
    if sys.platform != "win32":
        raise Error("Command is only supported on Windows!")

    entries = [
        # CLSID for VapourSynth VFW
        {
            "subkey": r"SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}",
            "value_name": "",
            "value_data": "VapourSynth",
        },
        {
            "subkey": r"SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32",
            "value_name": "",
            "value_data": PurePath(__file__).with_name("vsvfw.dll"),
        },
        {
            "subkey": r"SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32",
            "value_name": "ThreadingModel",
            "value_data": "Apartment",
        },
        # Media Type Extensions
        {
            "subkey": r"SOFTWARE\Classes\Media Type\Extensions\.vpy",
            "value_name": "",
            "value_data": "",
        },
        {
            "subkey": r"SOFTWARE\Classes\Media Type\Extensions\.vpy",
            "value_name": "Source Filter",
            "value_data": "{D3588AB0-0781-11CE-B03A-0020AF0BA770}",
        },
        # File association
        {
            "subkey": r"SOFTWARE\Classes\.vpy",
            "value_name": "",
            "value_data": "vpyfile",
        },
        {
            "subkey": r"SOFTWARE\Classes\vpyfile",
            "value_name": "",
            "value_data": "VapourSynth Python Script",
        },
        {
            "subkey": r"SOFTWARE\Classes\vpyfile\DefaultIcon",
            "value_name": "",
            "value_data": f"{PurePath(__file__).with_name('vsvfw.dll')},0",
        },
        # AVIFile Extensions
        {
            "subkey": r"SOFTWARE\Classes\AVIFile\Extensions\VPY",
            "value_name": "",
            "value_data": "{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}",
        },
    ]

    if not _write_registry_entries(entries):
        print("Couldn't register VFW provider!")
        sys.exit(1)
    else:
        print("VFW provider successfully registered!")


def vapoursynth_entrypoint():
    parser = argparse.ArgumentParser(prog="vapoursynth", description="VapourSynth configuration utility")
    operations = ["config", "check-env"]
    if sys.platform == "win32":
        operations = operations + ["register-install", "register-legacy-install", "register-vfw"]
    parser.add_argument("operation", choices=operations)
    args = parser.parse_args()

    command = args.operation

    if command == "config":
        vapoursynth_config()
    elif command == "check-env":
        vapoursynth_check_env()
    elif command == "register-install":
        register_install()
    elif command == "register-legacy-install":
        register_legacy_install()
    elif command == "register-vfw":
        register_vfw()


def vspipe():
    import subprocess

    vspipe_path = PurePath(__file__)
    vspipe_path = vspipe_path.with_name("vspipe")
    ret = subprocess.run([vspipe_path, *sys.argv[1:]])
    sys.exit(ret.returncode)
