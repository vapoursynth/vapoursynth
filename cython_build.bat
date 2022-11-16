rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.10-32 setup.py build_ext --inplace
py -3.10-32 setup.py bdist_wheel
py -3.10 setup.py build_ext --inplace
py -3.10 setup.py bdist_wheel
pause
