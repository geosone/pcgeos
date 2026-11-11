# PC/GEOS C port on top of the GHAL (GEOS Hardware Abstraction Layer).
# See Port32/PLAN.md and Port32/GHAL-Concept.md.
#
# C ports of asm files live next to the asm they replace; Port32/ only holds
# new material (docs, tools, tests).  The existing 16-bit/pm16 build still
# uses pmake (Port32/tools/geosbuild.sh) and is not touched by this file.
#
#   cmake -S . -B build -G Ninja                            # host linux, native
#   cmake -S . -B build32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-i686.cmake
#   cmake -S . -B build-dos32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-watcom.cmake
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(PCGEOS LANGUAGES C)

set(GEOS_HOSTS dos32 linux win32 win64 gonware)
set(GEOS_HOST linux CACHE STRING "GHAL host: ${GEOS_HOSTS}")
set_property(CACHE GEOS_HOST PROPERTY STRINGS ${GEOS_HOSTS})
if(NOT GEOS_HOST IN_LIST GEOS_HOSTS)
  message(FATAL_ERROR "GEOS_HOST must be one of: ${GEOS_HOSTS}")
endif()

set(GEOS_LINUX_DISPLAYS sdl3 x11 fbdev)
set(GEOS_LINUX_DISPLAY sdl3 CACHE STRING "Display backend for GEOS_HOST=linux: ${GEOS_LINUX_DISPLAYS}")
set_property(CACHE GEOS_LINUX_DISPLAY PROPERTY STRINGS ${GEOS_LINUX_DISPLAYS})
if(NOT GEOS_LINUX_DISPLAY IN_LIST GEOS_LINUX_DISPLAYS)
  message(FATAL_ERROR "GEOS_LINUX_DISPLAY must be one of: ${GEOS_LINUX_DISPLAYS}")
endif()

option(GEOS_ERROR_CHECK "Build the error-checking (EC) variant" ON)

if(CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  # dos32: Open Watcom 386 (C99 mode, warning level 4)
  add_compile_options(-za99 -w4)
else()
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()
if(GEOS_ERROR_CHECK)
  add_compile_definitions(DO_ERROR_CHECKING)   # GEOS convention; geos.h derives ERROR_CHECK
endif()
string(TOUPPER "${GEOS_HOST}" _host)
add_compile_definitions(GEOS_HOST_${_host})

message(STATUS "GEOS_HOST=${GEOS_HOST} arch=${CMAKE_SYSTEM_PROCESSOR} EC=${GEOS_ERROR_CHECK}")

enable_testing()
add_subdirectory(Port32/ghal)
add_subdirectory(Library/Math/Float)
add_subdirectory(Port32/Tests)

# Inventory of direct host contacts (plan P2).  Not part of 'all'.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(ghal-inventory
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_inventory.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --markdown ${CMAKE_BINARY_DIR}/GHAL-Inventory.md
            --json ${CMAKE_BINARY_DIR}/ghal_inventory.json
    COMMENT "Scanning GEOS sources for direct host contacts"
    VERBATIM)
  add_custom_target(ghal-inventory-check
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_inventory.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --check ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_baseline.json
    COMMENT "Checking for new host contacts against the baseline"
    VERBATIM)
endif()
