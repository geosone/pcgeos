# Host-side tests for the C ports and the GHAL (plan phase P1).
add_executable(test_ffa test_ffa.c)
target_link_libraries(test_ffa PRIVATE geos_math_float)
add_test(NAME math.floatFloatToAscii COMMAND test_ffa)

# Host-independent GHAL tests: run on every host, dos32 included.
add_executable(test_ghal_core test_ghal_core.c)
target_link_libraries(test_ghal_core PRIVATE ghal)
add_test(NAME ghal.core COMMAND test_ghal_core)

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
