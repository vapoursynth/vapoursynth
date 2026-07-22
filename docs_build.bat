pushd doc
py -3.14 -m sphinx -b html -d _build/doctrees . _build/html
if not defined CI pause
popd
