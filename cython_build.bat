rmdir /s /q build
del vapoursynth.*.pyd
"C:\Program Files (x86)\Python36-32\python.exe" setup.py build_ext --inplace
"C:\Program Files\Python36\python.exe" setup.py build_ext --inplace
pause