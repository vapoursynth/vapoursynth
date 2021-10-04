git clone https://github.com/vapoursynth/vsrepo
git clone https://github.com/sekrit-twc/zimg --branch v3.0
git clone https://github.com/AviSynth/AviSynthPlus.git
git clone https://github.com/sekrit-twc/libp2p.git
pushd msvc_project
msbuild VapourSynth.sln /t:Clean;Build -property:Configuration=Release;Platform=x64 -m
msbuild VapourSynth.sln /t:Clean;Build -property:Configuration=Release;Platform=Win32 -m
pause
popd
