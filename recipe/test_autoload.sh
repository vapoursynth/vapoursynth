set -ex

case "$target_platform" in
    osx-*)
        $CC -shared -o libcrash-plugin.dylib -fPIC crash-plugin.c -I$PREFIX/include/vapoursynth
        cp libcrash-plugin.dylib $PREFIX/lib/vapoursynth/libcrash-plugin.dylib;;
    *)
        $CC -shared -o libcrash-plugin.so -fPIC crash-plugin.c -I$PREFIX/include/vapoursynth
        cp libcrash-plugin.so $PREFIX/lib/vapoursynth/libcrash-plugin.so;;
esac

set +e  # expect an error exit here
"$PYTHON" -c "from vapoursynth import core; dir(core)"
[ $? -eq 3 ] && exit 0 || exit 1
