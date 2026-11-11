# Host-side tests for the C ports and the GHAL (plan phase P1).
if(NOT GEOS_HOST STREQUAL "dos32")
  add_executable(test_ffa test_ffa.c)
  target_link_libraries(test_ffa PRIVATE geos_math_float)
  add_test(NAME math.floatFloatToAscii COMMAND test_ffa)
endif()

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
