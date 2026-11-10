# C port of parts of the float library (Library/Math/Float/*.asm).
#   floatFloatToAscii.c  <- floatFloatToAscii.asm
#   fpstack.c            <- float stack routines of floatLow/High/Fixed/Comp.asm
# The asm build (pmake) lists its sources explicitly and is not affected.
add_library(geos_math_float STATIC
  floatFloatToAscii.c
  fpstack.c)
target_include_directories(geos_math_float PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(geos_math_float PUBLIC ${MATH_LIBRARY})
endif()
