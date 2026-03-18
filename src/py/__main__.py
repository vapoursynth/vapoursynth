import sys

from vapoursynth import register_install, register_vfw, vspipe, vapoursynth_check_env, vapoursynth_config


def main():
    if len(sys.argv) < 2:
        print("Usage: python -m mymodule <command>")
        print("Available commands:")
        print("  config")
        print("  check-env")
        print("  register-install")
        print("  register-vfw")
        print("  vspipe")
        sys.exit(1)

    command = sys.argv[1]

    sys.argv = [sys.argv[0]] + sys.argv[2:]

    if command == "config":
        vapoursynth_config()
    elif command == "check-env":
        vapoursynth_check_env()
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
