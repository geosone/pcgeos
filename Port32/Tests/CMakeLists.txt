# Host-side tests for the C ports (plan phase P1).
add_executable(test_ffa test_ffa.c)
target_link_libraries(test_ffa PRIVATE geos_math_float)
add_test(NAME math.floatFloatToAscii COMMAND test_ffa)

add_executable(test_ghal test_ghal.c)
target_link_libraries(test_ghal PRIVATE ghal)
add_test(NAME ghal.core COMMAND test_ghal)
