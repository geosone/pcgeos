# GeosNm.cmake -- the global symbols object files define, as nm lists
# them, in a file (for kexports, Library/Kernel/CMakeLists.txt).
#
#   cmake -D NM=<nm> -D OUT=<file> "-D OBJECTS=<a.o;b.o;...>" -P GeosNm.cmake
execute_process(COMMAND ${NM} -g --defined-only ${OBJECTS}
                OUTPUT_FILE ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${NM} failed")
endif()
