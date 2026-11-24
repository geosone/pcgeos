# GeosUicDeps.cmake -- the files a .ui file includes, as a make
# dependency file for the output of uic (run by geos_uic, GeosUic.cmake).
#
#   cmake -D UICPP=<uicpp> -D IN=<file.ui> -D OUT=<depfile>
#         -D TARGET=<uic's output> "-D ARGS=<-I and -D options>"
#         -P GeosUicDeps.cmake
execute_process(COMMAND ${UICPP} -M ${ARGS} ${IN}
                OUTPUT_VARIABLE deps RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "uicpp -M ${IN} failed")
endif()
# "<name>.o: files": the rule is for uic's output, and the files are
# named from the directory of the .ui file
string(REGEX REPLACE "^[^:]*:" "" deps "${deps}")
string(REGEX REPLACE "[ \t\r\n\\]+" ";" deps "${deps}")
get_filename_component(dir ${IN} DIRECTORY)
set(text "${TARGET}:")
foreach(f IN LISTS deps)
  if(NOT f STREQUAL "")
    get_filename_component(f ${f} ABSOLUTE BASE_DIR ${dir})
    string(APPEND text " \\\n  ${f}")
  endif()
endforeach()
file(WRITE ${OUT} "${text}\n")
