# The headers of CInclude under GEOS_PORT32.
# Base types of GEOS in C (geosport.h through geos.h), on every host.
geos_test_program(test_geosport LIBS ghal DEFINITIONS GEOS_PORT32
                  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude)
geos_add_test(cinclude.geosport test_geosport)

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
    ${PROJECT_SOURCE_DIR}/CInclude)
endif()
