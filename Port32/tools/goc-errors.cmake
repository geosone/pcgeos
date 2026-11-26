# goc-errors.cmake -- cmake -D GOC=<goc> -D IN=<file.goc> -D OUT=<file.c>
#   -P goc-errors.cmake
# goc on a file with errors must fail, stop, and print a bounded number
# of errors (GOC_MAX_ERRORS, Tools/goc/goc.h): its parser could stay at
# the same token and print the same error for ever (ctest
# goc.errors.stop, Port32/Tests/Goc/gocerrloop.goc).
execute_process(COMMAND ${GOC} -cg -o ${OUT} ${IN}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
string(REGEX MATCHALL "Error [^\n]*" errors "${out}${err}")
list(LENGTH errors n)
message("goc: status ${rc}, ${n} errors")
if(rc EQUAL 0 OR NOT rc MATCHES "^[0-9]+$")
  message(FATAL_ERROR "goc did not fail (or did not stop): ${rc}")
endif()
if(n EQUAL 0 OR n GREATER 100)
  message(FATAL_ERROR "goc printed ${n} errors")
endif()
if(EXISTS ${OUT})
  message(FATAL_ERROR "goc left ${OUT}")
endif()
