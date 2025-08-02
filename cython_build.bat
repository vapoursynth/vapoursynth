rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.13 -m build
py -3.8 -m build
pause
