rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.13 -m build
ren pyproject.toml pyproject_.toml
py -3.8 setup.py bdist_wheel
ren pyproject_.toml pyproject.toml
pause
