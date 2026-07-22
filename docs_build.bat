pushd doc
sphinx-build -b html -d _build/doctrees . _build/html
if not defined CI pause
popd
