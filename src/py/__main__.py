import sys

from vapoursynth import register_install, register_vfw, vspipe, vsscript_check_env, vsscript_config


def main():
    if len(sys.argv) < 2:
        print("Usage: python -m mymodule <command>")
        print("Available commands:")
        print("  vsscript-config")
        print("  vsscript-check-env")
        print("  register-install")
        print("  register-vfw")
        print("  vspipe")
        sys.exit(1)

    command = sys.argv[1]

    sys.argv = [sys.argv[0]] + sys.argv[2:]

    if command == "vsscript-config":
        vsscript_config()
    elif command == "vsscript-check-env":
        vsscript_check_env()
    elif command == "register-install":
        register_install()
    elif command == "register-vfw":
        register_vfw()
    elif command == "vspipe":
        vspipe()
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)


if __name__ == "__main__":
    main()
