#!/bin/bash

set -ex

nproc=$(getconf _NPROCESSORS_ONLN)

mkdir build-msvc

(
cd build-msvc
cmake .. -DWITH_QT6=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.6.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/usr/x86_64-pc-windows-msvc/toolchain-x86_64.cmake -DSOUND=NONE -DWITH_BUNDLED_FMT=ON
make -j${nproc}
)
