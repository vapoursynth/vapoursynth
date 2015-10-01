rmdir /s /q build
del vapoursynth.*.pyd
"C:\Program Files (x86)\Python 3.5\python.exe" setup.py build_ext --inplace
"C:\Program Files\Python 3.5\python.exe" setup.py build_ext --inplace
pause