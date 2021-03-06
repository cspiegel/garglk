set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(MINGW_TRIPLE_DEFAULT "i686-w64-mingw32")
set(MINGW_TRIPLE ${MINGW_TRIPLE_DEFAULT} CACHE STRING "Target triple of MinGW compiler. Default: ${MINGW_TRIPLE_DEFAULT}.")
set(MINGW_PREFIX "/usr/${MINGW_TRIPLE}" CACHE STRING "Prefix of MinGW installation. Default: /usr/${MINGW_TRIPLE}.")

set(CMAKE_C_COMPILER "${MINGW_TRIPLE}-gcc")
set(CMAKE_CXX_COMPILER "${MINGW_TRIPLE}-g++")
set(CMAKE_RC_COMPILER "${MINGW_TRIPLE}-windres")

set(CMAKE_FIND_ROOT_PATH ${MINGW_PREFIX})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
