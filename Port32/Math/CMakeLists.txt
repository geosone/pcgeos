# Pilot: floatFloatToAscii.asm translated to C, plus the float-stack shim.
add_library(geos_math_ffa STATIC
  floatFloatToAscii.c
  fpstack.c)
target_include_directories(geos_math_ffa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(geos_math_ffa PUBLIC ${MATH_LIBRARY})
endif()

add_executable(test_ffa test_ffa.c)
target_link_libraries(test_ffa PRIVATE geos_math_ffa)
add_test(NAME math.floatFloatToAscii COMMAND test_ffa)
