:: Clean
rmdir /s /q build 2>nul
del /q vapoursynth.*.pyd 2>nul
del /q dist\*.whl 2>nul
:: Build
"C:\Program Files (x86)\Python37-32\python.exe" setup.py build_ext --inplace
"C:\Program Files\Python37\python.exe" setup.py build_ext --inplace
"C:\Program Files (x86)\Python37-32\python.exe" setup.py bdist_wheel
"C:\Program Files\Python37\python.exe" setup.py bdist_wheel