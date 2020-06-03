rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py.exe -3.8-32 setup.py build_ext --inplace
py.exe -3.8-32 setup.py bdist_wheel
py.exe -3.8 setup.py build_ext --inplace
py.exe -3.8 setup.py bdist_wheel
pause
