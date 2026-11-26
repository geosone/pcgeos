# goc -cg: object resources, class structures and chunk handles of a .goc
# geode in two files, linked and checked on the host (gocres*.goc).
# goc -cg needs C99 compound literals, which Open Watcom lacks: dos32 is
# built with DJGPP gcc for the port (decided 2026-11-11, ABI.md section 5).
if(NOT GEOS_HAVE_GOC)
  return()
endif()
geos_goc(_gocres_c gocres.gp gocres.goc gocres2.goc)
# test_gocres.c stands in for the message kernel (fake handles show
# where goc sends to): its entry points get their own names, so the
# real ones of geos_kernel (used for LMem) do not clash
geos_test_program(test_gocres SOURCES ${_gocres_c} gocres_lmem.c
  DEFINITIONS GEOS_PORT32
    CObjMessageP=GocTestObjMessageP CObjCallSuperP=GocTestObjCallSuperP
    CMessageDispatchP=GocTestMessageDispatchP
  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # parameters of methods are often unused
  target_compile_options(test_gocres PRIVATE -Wno-unused-parameter)
endif()
geos_add_test(goc.resources test_gocres)

# classes and objects of goc -cg for the kernel's message and object
# tests (../Kernel, which builds them: kmsg.goc, kobj.goc)

# goc stops after too many errors, with an exit status, its output
# removed (Tools/goc: GocTooManyErrors)
add_test(NAME goc.errors.stop
         COMMAND ${CMAKE_COMMAND} -D GOC=${GEOS_GOC}
                 -D IN=${CMAKE_CURRENT_SOURCE_DIR}/gocerrloop.goc
                 -D OUT=${CMAKE_CURRENT_BINARY_DIR}/gocerrloop.c
                 -P ${PROJECT_SOURCE_DIR}/Port32/tools/goc-errors.cmake)
set_tests_properties(goc.errors.stop PROPERTIES TIMEOUT 60)
