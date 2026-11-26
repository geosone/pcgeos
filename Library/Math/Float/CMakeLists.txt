# C port of parts of the float library (Library/Math/Float/*.asm).
#   floatFloatToAscii.c  <- floatFloatToAscii.asm
#   floatStack.c         <- floatStack.asm: the float stack itself
#   float*.c             <- the routines on it, each after its asm file
# The asm build (pmake) lists its sources explicitly and is not affected.
add_library(geos_math_float STATIC
  floatFloatToAscii.c
  floatStack.c
  floatTrans.c
  floatLow.c
  floatHigh.c
  floatFixed.c
  floatConstants.c
  floatComp.c)
target_include_directories(geos_math_float PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(geos_math_float PUBLIC ${MATH_LIBRARY})
endif()
