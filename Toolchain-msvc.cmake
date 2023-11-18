set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(CMAKE_C_COMPILER "x86_64-pc-windows-msvc-gcc")
set(CMAKE_CXX_COMPILER "x86_64-pc-windows-msvc-g++")
set(CMAKE_EXE_LINKER_FLAGS "/winsdkdir:/home/chris/Programming/XWin/winsdk/sdk /vctoolsdir:/home/chris/Programming/XWin/winsdk/crt /libpath:/home/chris/Programming/XWin/winsdk/sdk/lib/um/x64")
set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
set(CMAKE_MT "llvm-mt")

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-pc-windows-msvc)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-pc-windows-msvc/lib/pkgconfig")
