rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.14 -m build
pause
