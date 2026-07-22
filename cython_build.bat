@echo off

rem Build with clang-cl, the project's preferred compiler on Windows. Meson otherwise
rem auto-detects and uses MSVC cl; setting CC/CXX selects clang-cl instead and --vsenv
rem still activates the Visual Studio environment so ninja and the linker are found
rem (meson skips that auto-activation once a compiler is already specified).
if not defined VSCLANGDIR set "VSCLANGDIR=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin"
if not exist "%VSCLANGDIR%\clang-cl.exe" set "VSCLANGDIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Tools\Llvm\x64\bin"
if not exist "%VSCLANGDIR%\clang-cl.exe" (
    echo clang-cl not found, install the "C++ Clang tools for Windows" Visual Studio component
    echo or set VSCLANGDIR to the directory containing clang-cl.exe
    exit /b 1
)
set "PATH=%VSCLANGDIR%;%PATH%"
set CC=clang-cl
set CXX=clang-cl

rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.14 -m build --sdist --wheel -Csetup-args=--debug -Csetup-args=--vsenv
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)
pushd dist
for %%i in (*.whl) do wheel tags --remove --python-tag cp312 %%i
popd
pause
