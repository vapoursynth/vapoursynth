rmdir /s /q build
del vapoursynth.*.pyd
"C:\Program Files (x86)\Python35-32\python.exe" setup.py build_ext --inplace
"C:\Program Files\Python35\python.exe" setup.py build_ext --inplace
pause