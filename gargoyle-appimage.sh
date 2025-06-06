#!/bin/bash

set -ex

# https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
# https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage
# https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

frankendrift="OFF"

while getopts "cf" o
do
    case "${o}" in
        c)
            clean=1
            ;;
        f)
            frankendrift="ON"
            ;;
        *)
            fatal "Usage: $0 [-f]"
            ;;
    esac
done

[[ -n "${clean}" ]] && rm -rf build-appimage build/dist

mkdir build-appimage
cd build-appimage
xargs -n 1 -P 0 wget -q <<EOF
https://github.com/garglk/assets/raw/appimage/linuxdeploy-x86_64.AppImage
https://github.com/garglk/assets/raw/appimage/linuxdeploy-plugin-appimage-x86_64.AppImage
https://github.com/garglk/assets/raw/appimage/linuxdeploy-plugin-qt-x86_64.AppImage
EOF
chmod +x linuxdeploy*
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DAPPIMAGE=TRUE -DBUILD_SHARED_LIBS=OFF -DWITH_FRANKENDRIFT="${frankendrift}"
make "-j$(nproc)"
make install DESTDIR=AppDir

export QMAKE=qmake6

OUTPUT=../Gargoyle-x86_64.AppImage ./linuxdeploy-x86_64.AppImage \
    --appdir=AppDir \
    --plugin qt \
    --icon-file=../garglk/gargoyle-house.png \
    --icon-filename=io.github.garglk.Gargoyle \
    -d ../garglk/gargoyle.desktop \
    --output=appimage
