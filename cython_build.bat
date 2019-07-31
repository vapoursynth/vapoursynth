rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
"C:\Program Files (x86)\Python37-32\python.exe" setup.py build_ext --inplace
"C:\Program Files\Python37\python.exe" setup.py build_ext --inplace
"C:\Program Files (x86)\Python37-32\python.exe" setup.py bdist_wheel
"C:\Program Files\Python37\python.exe" setup.py bdist_wheel
pause