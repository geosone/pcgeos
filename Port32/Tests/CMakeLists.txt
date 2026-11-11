# Host-side tests for the C ports and the GHAL (plan phase P1).
add_executable(test_ffa test_ffa.c)
target_link_libraries(test_ffa PRIVATE geos_math_float)
add_test(NAME math.floatFloatToAscii COMMAND test_ffa)

# Host-independent GHAL tests: run on every host, dos32 included.
add_executable(test_ghal_core test_ghal_core.c)
target_link_libraries(test_ghal_core PRIVATE ghal)
add_test(NAME ghal.core COMMAND test_ghal_core)

# ghal_module: a geode in the host's module format (ELF, or DXE3 on dos32),
# embedded in the test program.  Not with Open Watcom (no geode toolchain).
if(NOT CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)
  geos_geode(testgeode Geode/testgeode.c)
  geos_embed(_testgeode_c ${testgeode_GEO} testgeode_image)
  add_executable(test_ghal_module test_ghal_module.c ${_testgeode_c})
  add_dependencies(test_ghal_module geode_testgeode)
  target_link_libraries(test_ghal_module PRIVATE ghal)
  add_test(NAME ghal.module COMMAND test_ghal_module)
endif()

if(GEOS_HOST STREQUAL "linux")
  add_executable(test_ghal_posix test_ghal_posix.c)
  target_link_libraries(test_ghal_posix PRIVATE ghal)
  add_test(NAME ghal.posix COMMAND test_ghal_posix)
endif()

if(GEOS_HOST STREQUAL "dos32")
  add_executable(test_ghal_dos32 test_ghal_dos32.c)
  target_link_libraries(test_ghal_dos32 PRIVATE ghal)
  add_test(NAME ghal.dos32 COMMAND test_ghal_dos32)
endif()

# The expected strings of test_ffa must be those of the original GEOS math
# library (reference output in GeosRef/ffaref.txt, see GeosRef/README.md).
# Runs on the build host, also when cross compiling.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME math.floatFloatToAscii.geosref
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/check_ref.py
                   ${CMAKE_CURRENT_SOURCE_DIR}/ffa_cases.h
                   ${CMAKE_CURRENT_SOURCE_DIR}/test_ffa.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/ffaref.txt)
endif()

# Base types of GEOS in C (geosport.h through geos.h), on every host.
add_executable(test_geosport test_geosport.c)
target_compile_definitions(test_geosport PRIVATE GEOS_PORT32)
target_include_directories(test_geosport PRIVATE
  ${PROJECT_SOURCE_DIR}/Port32/include ${PROJECT_SOURCE_DIR}/CInclude)
add_test(NAME cinclude.geosport COMMAND test_geosport)

# Every header of cinclude_port32.txt must compile on its own (after
# geos.h) under GEOS_PORT32 without warnings: one translation unit each.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/cinclude_port32.txt _hdrs
       REGEX "^[^#]")
  set(_tus)
  foreach(_h IN LISTS _hdrs)
    string(MAKE_C_IDENTIFIER "${_h}" _id)
    set(_tu ${CMAKE_CURRENT_BINARY_DIR}/cinclude/${_id}.c)
    file(CONFIGURE OUTPUT ${_tu}
         CONTENT "#include <geos.h>\n#include <${_h}>\n" @ONLY)
    list(APPEND _tus ${_tu})
  endforeach()
  add_library(cinclude_port32 OBJECT ${_tus})
  target_compile_definitions(cinclude_port32 PRIVATE GEOS_PORT32)
  target_compile_options(cinclude_port32 PRIVATE -Werror)
  target_include_directories(cinclude_port32 PRIVATE
    ${PROJECT_SOURCE_DIR}/Port32/include ${PROJECT_SOURCE_DIR}/CInclude)
endif()

# goc -cg: object resources, class structures and chunk handles of a .goc
# geode in two files, linked and checked on the host (Goc/gocres*.goc).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
# goc -cg needs C99 compound literals, which Open Watcom lacks: dos32 is
# built with DJGPP gcc for the port (decided 2026-11-11, ABI.md section 5).
if(GEOS_GOC AND Python3_Interpreter_FOUND AND NOT CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  geos_goc(_gocres_c Goc/gocres.gp Goc/gocres.goc Goc/gocres2.goc)
  add_executable(test_gocres ${_gocres_c} Goc/test_gocres.c)
  target_compile_definitions(test_gocres PRIVATE GEOS_PORT32)
  target_include_directories(test_gocres PRIVATE
    ${PROJECT_SOURCE_DIR}/Port32/include ${PROJECT_SOURCE_DIR}/CInclude)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # parameters of methods are often unused
    target_compile_options(test_gocres PRIVATE -Wno-unused-parameter)
  endif()
  add_test(NAME goc.resources COMMAND test_gocres)
endif()

# The SDK_C samples of goc_sdk.txt must keep compiling through goc -cg
# (compile only: linking needs the ported kernel and UI).  Only errors
# count here; the samples are old code with many harmless warnings.
if(GEOS_GOC AND Python3_Interpreter_FOUND AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/goc_sdk.txt _samples REGEX "^[^#]")
  set(_sdk_srcs)
  foreach(_s IN LISTS _samples)
    set(_dir ${PROJECT_SOURCE_DIR}/Appl/SDK_C/${_s})
    file(GLOB _gp ${_dir}/*.gp)
    file(GLOB _gocs ${_dir}/*.goc)
    list(GET _gp 0 _gp)
    geos_goc(_c ${_gp} ${_gocs})
    set_source_files_properties(${_c} PROPERTIES INCLUDE_DIRECTORIES ${_dir})
    list(APPEND _sdk_srcs ${_c})
  endforeach()
  add_library(goc_sdk OBJECT ${_sdk_srcs})
  target_compile_definitions(goc_sdk PRIVATE GEOS_PORT32)
  target_include_directories(goc_sdk PRIVATE
    ${PROJECT_SOURCE_DIR}/Port32/include ${PROJECT_SOURCE_DIR}/CInclude)
  target_compile_options(goc_sdk PRIVATE -w)
endif()
