rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
py -3.14 -m build --sdist --wheel -Csetup-args=--debug
pushd dist
for %%i in (*.whl) do wheel tags --remove --python-tag cp312 %%i
popd
pause
