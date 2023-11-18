set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(CMAKE_EXE_LINKER_FLAGS "-libpath:/home/chris/Programming/XWin/winsdk/crt/lib/x64 -libpath:/home/chris/Programming/XWin/winsdk/crt/atlmfc/lib/x64 -libpath:/home/chris/Programming/XWin/winsdk/sdk/Lib/10.0.22621/ucrt/x64 -libpath:/home/chris/Programming/XWin/winsdk/sdk/Lib/10.0.22621/um/x64")

# cmake .. -DWITH_QT6=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.6.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../Toolchain-msvc.cmake -DSOUND=NONE -DWITH_BUNDLED_FMT=ON

# set(MINGW_TRIPLE $ENV{MINGW_TRIPLE})
# set(MINGW_PREFIX "$ENV{MINGW_LOCATION}/${MINGW_TRIPLE}")

set(CMAKE_C_COMPILER "x86_64-pc-windows-msvc-gcc")
set(CMAKE_CXX_COMPILER "x86_64-pc-windows-msvc-g++")
# set(CMAKE_C_COMPILER "clang-cl")
# set(CMAKE_C_FLAGS "/winsdkdir /home/chris/Programming/XWin/winsdk/sdk /vctoolsdir /home/chris/Programming/XWin/winsdk/crt")
# set(CMAKE_CXX_COMPILER "clang-cl")
# set(CMAKE_CXX_FLAGS "/winsdkdir /home/chris/Programming/XWin/winsdk/sdk /vctoolsdir /home/chris/Programming/XWin/winsdk/crt")
set(CMAKE_EXE_LINKER_FLAGS "-libpath:/home/chris/Programming/XWin/winsdk/crt/lib/x64 -libpath:/home/chris/Programming/XWin/winsdk/crt/atlmfc/lib/x64 -libpath:/home/chris/Programming/XWin/winsdk/sdk/Lib/10.0.22621/ucrt/x64 -libpath:/home/chris/Programming/XWin/winsdk/sdk/Lib/10.0.22621/um/x64")
set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
# set(CMAKE_RC_COMPILER "x86_64-w64-mingw32-windres")
set(CMAKE_MT "llvm-mt")

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-pc-windows-msvc)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-pc-windows-msvc/lib/pkgconfig")
