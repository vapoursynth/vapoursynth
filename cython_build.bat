:: Clean
rmdir /s /q build 2>nul
del /q vapoursynth.*.pyd 2>nul
del /q dist\*.whl 2>nul
:: Build
if not "%PLATFORM%" == "x64" (
    "%PYTHON32%/python.exe" setup.py build_ext --inplace
    "%PYTHON32%/python.exe" setup.py bdist_wheel
)
if not "%PLATFORM%" == "Win32" (
    "%PYTHON64%/python.exe" setup.py build_ext --inplace
    "%PYTHON64%/python.exe" setup.py bdist_wheel
)