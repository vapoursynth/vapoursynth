rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py.exe -3.7-32 setup.py build_ext --inplace
py.exe -3.7-32 setup.py bdist_wheel
py.exe -3.7 setup.py build_ext --inplace
py.exe -3.7 setup.py bdist_wheel
pause
