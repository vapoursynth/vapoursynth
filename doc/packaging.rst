Plugin Packaging
****************

As a plugin author, it is recommended and greatly appreciated to offer pre-compiled binaries as Python wheels.
VapourSynth can automatically discover them, and Python scripts or packages can reliably depend on them as declared dependencies.

This section covers the minimum steps required to package and ship your plugin as a wheel, as well as publishing it on PyPI.
Templates and examples are also provided.

The theory
==========

Packaging a plugin as a wheel and making it discoverable by VapourSynth can simply be summarized as
"zip this library and install it at this location."

A ``.whl`` file can be very roughly described as: everything inside it gets unzipped into ``site-packages``.
As such, VapourSynth automatically and recursively loads all native plugins located in ``<site-packages>/vapoursynth/plugins``.
Thus, wheels must install their native files at this location.

In practice
===========

A very simple project structure would look like this:

::

    path/to/your/project
    ├── src
    |  └── MyPlugin
    |     ├── myplugin.cpp
    |     └── myplugin.h
    ├── .gitattributes
    ├── .gitignore
    ├── LICENSE
    ├── hatch_build.py
    ├── meson.build
    ├── pyproject.toml
    └── README.md

In this simple project, we use `Hatchling <https://hatch.pypa.io/latest/>`_ as the Python build backend.
The custom Hatch build hook (``hatch_build.py``) invokes Meson to compile the native plugin,
then copies the resulting binary into a ``vapoursynth/plugins/`` directory.

This directory is declared in ``pyproject.toml`` so that Hatchling includes it in the final wheel.

When running ``python -m build``, the ``build`` package reads the ``pyproject.toml`` file, parses the metadata,
and lets Hatchling create the source distribution (sdist) and the wheel.

Steps
=====

Assuming your project is already finished and ready to deploy, let's create the Python project structure.
Several build frontends such as `uv <https://docs.astral.sh/uv/>`_, `Poetry <https://python-poetry.org/>`_, or `pipx <https://pipx.pypa.io/stable/>`_
can make these steps easier or faster, but they also require additional knowledge.
For simplicity, we use native tools here.

Create a `virtual environment <https://docs.python.org/3/library/venv.html>`_::

    python -m venv .venv

Activate the virtual environment. On Windows::

    .venv\Scripts\activate

On Linux/macOS::

    source .venv/bin/activate

At this point you will need to write the ``pyproject.toml`` and a custom Hatch build hook.

The ``pyproject.toml``:

.. code-block:: toml

    [build-system]
    requires = ["hatchling", "packaging", "meson"]
    build-backend = "hatchling.build"

    [project]
    name = "MyPlugin"
    version = "1.0"
    description = "MyPlugin description"
    requires-python = ">=3.12"
    readme = "README.md"
    license = "MIT"
    license-files = ["LICENSE"]
    authors = [{ name = "Name", email = "name@email.com" }]
    maintainers = [{ name = "YourName", email = "name@email.com" }]

    dependencies = ["vapoursynth>=74"]

    [tool.hatch.build.targets.wheel]
    include = ["vapoursynth/plugins"]
    artifacts = [
        "vapoursynth/plugins/*.dylib",
        "vapoursynth/plugins/*.so",
        "vapoursynth/plugins/*.dll",
    ]

    [tool.hatch.build.targets.wheel.hooks.custom]
    path = "hatch_build.py"

The ``include`` directive tells Hatchling to package the ``vapoursynth/plugins/`` directory into the wheel.
The ``artifacts`` list specifies which compiled binary extensions to include.
When the wheel is installed, these files end up in ``<site-packages>/vapoursynth/plugins/``, where VapourSynth discovers them.

The custom Hatch build hook (``hatch_build.py``):

.. code-block:: python

    import shutil
    import subprocess
    import sys
    from pathlib import Path
    from typing import Any

    from hatchling.builders.hooks.plugin.interface import BuildHookInterface
    from packaging import tags


    class CustomHook(BuildHookInterface[Any]):
        """
        Custom build hook to compile the Meson project and package the resulting binaries.
        """

        source_dir = Path("build")
        target_dir = Path("vapoursynth/plugins")

        def initialize(self, version: str, build_data: dict[str, Any]) -> None:
            """
            Called before the build process starts.
            Sets build metadata and executes the Meson compilation.
            """
            # https://hatch.pypa.io/latest/plugins/builder/wheel/#build-data
            build_data["pure_python"] = False

            # Custom platform tagging logic:
            # We avoid the default 'infer_tag' (e.g., cp314-cp314-win_amd64) to prevent needing a separate wheel
            # for every Python version.
            # Since the compiled plugin only depends on the VapourSynth API and the OS/architecture,
            # we use a more generic tag: 'py3-none-<platform>'.
            #
            # NOTE:
            # For multi-platform distribution, this script should be run in a CI environment (like cibuildwheel)
            # or driven by environment variables to inject the appropriate platform tags.
            build_data["tag"] = f"py3-none-{next(tags.platform_tags())}"

            # Setup with vsenv
            # The ``--vsenv`` flag in the Meson setup command activates the Visual Studio environment on Windows,
            # which is required for MSVC-based compilation. On Linux and macOS, this flag is safely ignored.
            subprocess.run([sys.executable, "-m", "mesonbuild.mesonmain", "setup", "build", "--vsenv"], check=True)

            # Compile
            subprocess.run([sys.executable, "-m", "mesonbuild.mesonmain", "compile", "-C", "build"], check=True)

            # Ensure the target directory exists and copy the compiled binaries
            self.target_dir.mkdir(parents=True, exist_ok=True)
            for file_path in self.source_dir.glob("*"):
                if file_path.is_file() and file_path.suffix in [".dll", ".so", ".dylib"]:
                    shutil.copy2(file_path, self.target_dir)

        def finalize(self, version: str, build_data: dict[str, Any], artifact_path: str) -> None:
            """
            Called after the build process finishes.
            Cleans up temporary build artifacts.
            """
            shutil.rmtree(self.target_dir.parent, ignore_errors=True)

.. warning::

    The platform tag logic (``py3-none-<platform>``) produces a wheel tied to the current OS and architecture.
    For multi-platform distribution, use a CI tool like `cibuildwheel <https://cibuildwheel.pypa.io/en/stable/>`_
    to build separate wheels for each target platform.

Install the ``build`` package and build the wheel::

    pip install build
    python -m build

The resulting wheel will be in the ``dist/`` directory, ready for distribution.

You can of course customize the ``pyproject.toml`` metadata further by adding classifiers, keywords, and URLs,
as well as setting up automatic version detection or including your own Python wrapper package.

Publishing to PyPI
==================

Once your wheel is built, you can publish it to `PyPI <https://pypi.org/>`_ so that users can install your plugin with ``pip install MyPlugin``.

The recommended approach is to use `Trusted Publishing <https://docs.pypi.org/trusted-publishers/>`_ via GitHub Actions,
which eliminates the need to manage API tokens manually.
For a step-by-step guide, refer to the `PyPA publishing tutorial <https://packaging.python.org/en/latest/tutorials/packaging-projects/#uploading-the-distribution-archives>`_.

If you prefer to publish manually, you can use `twine <https://twine.readthedocs.io/en/stable/>`_::

    pip install twine
    twine upload dist/*

You will be prompted for your PyPI credentials or API token.

Automating the process with CI
==============================

Tools such as `cibuildwheel <https://cibuildwheel.pypa.io/en/stable/>`_ can greatly ease the automation process to deliver wheels for all three platforms.

Some examples:

- `VapourSynth-EdgeMasks CI workflow <https://github.com/HolyWu/VapourSynth-EdgeMasks/blob/436651859e5a192e56304551686cfc75a5383c3b/.github/workflows/build.yml>`_
- `bestsource CI workflow <https://github.com/vapoursynth/bestsource/blob/e31fa7722706895109ae7a6b15fb3492e96402c0/.github/workflows/build.yml>`_

Concrete examples
=================

- `vs-package-poc <https://github.com/Ichunjo/vs-package-poc/tree/master>`_ — Multi-backend packaging Proof of Concept
- `VapourSynth-EdgeMasks <https://github.com/HolyWu/VapourSynth-EdgeMasks>`_ — Real-world plugin with simple CI
- `bestsource <https://github.com/vapoursynth/bestsource>`_ — Real-world plugin with complex CI
