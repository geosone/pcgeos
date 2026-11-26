# uic-goc.cmake -- cmake -D UIC=<uic> -D UICPP=<uicpp> -D GOC=<goc>
#   -D SRC=<Port32/Tests/Uic> -D OUT=<dir> -D TOP=<source tree>
#   -P uic-goc.cmake
# What uic -g makes of a gstring in Esp (Tools/uic/outgoc.c, ctest
# uic.goc.gstring), uicgstr.ui with TEST_BIG 0 and 1:
#   X label byte: the bytes before it, (EndTestBM-StartTestBM) the size
#   of the bitmap; Esp's if/else decided; 0101b in hex; BM_MASK BMT_MASK;
#   -N HINT_DEFAULT_DEFAULT_ACTION left out;
# and goc -cg takes it.  uicgstrbad.ui (labels across a GS macro) fails.
function(run_uic out big)
  execute_process(COMMAND ${UIC} -g -p ${UICPP} -I${TOP}/Include
                          -DTEST_BIG=${big} -N HINT_DEFAULT_DEFAULT_ACTION
                          -i Objects/gTrigC.goh -x ${out}.goh -o ${out}.goc
                          uicgstr.ui
    WORKING_DIRECTORY ${SRC} RESULT_VARIABLE rc ERROR_VARIABLE err
    TIMEOUT 60)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "uic -g (TEST_BIG ${big}): ${rc} ${err}")
  endif()
  execute_process(COMMAND ${GOC} -cg -D__GEOS__ -I${TOP}/CInclude
                          -o ${out}.c ${out}.goc
    RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE err TIMEOUT 60)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "goc on uic's output (TEST_BIG ${big}): ${rc} "
                        "${o}${err}")
  endif()
  file(READ ${out}.goc t)
  set(text "${t}" PARENT_SCOPE)
endfunction()

function(want what pattern)
  if(NOT text MATCHES "${pattern}")
    message(FATAL_ERROR "${what}: not in the output:\n${text}")
  endif()
endfunction()
function(not_want what pattern)
  if(text MATCHES "${pattern}")
    message(FATAL_ERROR "${what}: in the output:\n${text}")
  endif()
endfunction()

file(MAKE_DIRECTORY ${OUT})
run_uic(${OUT}/small 0)
want("the size of the small bitmap" "GSFillBitmapAtCP\\( *\\(12-0\\) *\\)")
want("its bytes in hex" "0x00, 0x0e, 0x00, 0x00, 0x08, 0x00,")
not_want("the other branch" "0xff|BMT_MASK")
want("a hint" "HINT_EXPAND_WIDTH_TO_FIT_PARENT;")
not_want("the hint of -N" "HINT_DEFAULT_DEFAULT_ACTION")
not_want("a label" "TestBM")

run_uic(${OUT}/big 1)
want("the size of the big bitmap" "GSFillBitmapAtCP\\( *\\(18-0\\) *\\)")
want("BM_MASK in C" "BMT_MASK \\| BMF_MONO")
want("its bytes in hex" "0xff, 0xff, 0xf0,")
not_want("the other branch" "0x0e")

execute_process(COMMAND ${UIC} -g -p ${UICPP} -x ${OUT}/bad.goh
                        -o ${OUT}/bad.goc uicgstrbad.ui
  WORKING_DIRECTORY ${SRC} RESULT_VARIABLE rc ERROR_VARIABLE err
  OUTPUT_VARIABLE o TIMEOUT 60)
if(rc EQUAL 0 OR NOT "${o}${err}" MATCHES "difference of labels")
  message(FATAL_ERROR "uicgstrbad.ui: uic did not fail as it should: "
                      "${rc} ${o}${err}")
endif()
message("uic -g: labels, conditions, numbers, -N as they should be")
