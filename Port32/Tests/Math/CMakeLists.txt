# The math library (Library/Math/Float).
geos_test_program(test_ffa LIBS geos_math_float)
geos_add_test(math.floatFloatToAscii test_ffa)

# The expected strings of test_ffa must be those of the original GEOS math
# library (reference output in GeosRef/ffaref.txt, see GeosRef/README.md).
# Runs on the build host, also when cross compiling.
if(Python3_Interpreter_FOUND)
  add_test(NAME math.floatFloatToAscii.geosref
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/check_ref.py
                   ${CMAKE_CURRENT_SOURCE_DIR}/ffa_cases.h
                   ${CMAKE_CURRENT_SOURCE_DIR}/test_ffa.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/ffaref.txt)
endif()
