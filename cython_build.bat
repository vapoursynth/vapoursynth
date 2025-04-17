rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.13 setup.py bdist_wheel
py -3.8 setup.py bdist_wheel
pause
