# Cross build for GEOS_HOST=dos32: 32-bit protected mode DOS programs with
# Open Watcom 386 and the DOS/32 Advanced extender (stub bound into the .exe).
#
#   cmake -S . -B build-dos32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-watcom.cmake
#   cmake --build build-dos32 && ctest --test-dir build-dos32
#
# Tests run headless in DOSBox-X through Port32/tools/dosrun.sh (a test
# harness for the DOS binaries; GEOS itself does not run emulated).
set(CMAKE_SYSTEM_NAME DOS)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT DEFINED ENV{WATCOM})
  set(ENV{WATCOM} /opt/watcom)
endif()
set(WATCOM_ROOT $ENV{WATCOM})
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(_wbin ${WATCOM_ROOT}/binl64)
else()
  set(_wbin ${WATCOM_ROOT}/binl)
endif()
set(CMAKE_C_COMPILER ${_wbin}/wcl386)
# wcl386 finds wcc386, and wlink the extender stub (dos32a.exe), along
# PATH; pass that environment to every compile and link step.
set(_wenv env "WATCOM=${WATCOM_ROOT}" "INCLUDE=${WATCOM_ROOT}/h"
    "PATH=${_wbin}:${WATCOM_ROOT}/binw:$ENV{PATH}")
set(CMAKE_C_COMPILER_LAUNCHER ${_wenv})
set(CMAKE_C_LINKER_LAUNCHER ${_wenv})
set(ENV{PATH} "${_wbin}:${WATCOM_ROOT}/binw:$ENV{PATH}")   # configure checks

set(GEOS_HOST dos32 CACHE STRING "GHAL host" FORCE)
set(CMAKE_USER_MAKE_RULES_OVERRIDE ${CMAKE_CURRENT_LIST_DIR}/dos32-watcom-rules.cmake)
set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_LIST_DIR}/../Port32/tools/dosrun.sh)

set(CMAKE_FIND_ROOT_PATH ${WATCOM_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
