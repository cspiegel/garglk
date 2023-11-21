#!/bin/bash

set -ex

nproc=$(getconf _NPROCESSORS_ONLN)

mkdir build-msvc

(
cd build-msvc
cmake .. -DWITH_QT6=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.6.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=Toolchain-msvc.cmake -DSOUND=QT -DWITH_BUNDLED_FMT=ON
make -j${nproc}
)
