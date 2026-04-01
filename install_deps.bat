@echo off

IF NOT EXIST libp2p (
    git clone https://github.com/sekrit-twc/libp2p
) ELSE (
    echo libp2p: & pushd libp2p & git pull &popd
)

IF NOT EXIST zimg (
    git clone https://github.com/sekrit-twc/zimg --shallow-submodules --recurse-submodules
) ELSE (
    echo zimg: & pushd zimg & git pull & popd
)

py -3.14 -m ensurepip
py -3.14 -m pip install --upgrade -r python-requirements.txt
