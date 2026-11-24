# The C kernel (Library/Kernel).
foreach(_t heap thread lmem region gstate transform file fileenum link
        initfile stdpath shutdown vm carray local timer sys)
  geos_test_program(test_kernel_${_t})
  geos_add_test(kernel.${_t} test_kernel_${_t})
endforeach()
# files with GEOS headers and extended attributes
geos_test_program(test_kernel_fileea)
geos_add_test(kernel.file.geos test_kernel_fileea)
# the window system and the input manager, with their internals
geos_test_program(test_kernel_win INCLUDES ${PROJECT_SOURCE_DIR}/Library/Kernel)
geos_add_test(kernel.win test_kernel_win)
geos_test_program(test_kernel_im INCLUDES ${PROJECT_SOURCE_DIR}/Library/Kernel)
geos_add_test(kernel.im test_kernel_im)
# the database manager; files of DOS16 GEOS with databases (not inside
# the DOS machine)
geos_test_program(test_kernel_db)
if(GEOS_HOST STREQUAL "dos32")
  geos_add_test(kernel.db test_kernel_db)
else()
  geos_add_test(kernel.db test_kernel_db
    ${PROJECT_SOURCE_DIR}/Appl/Breadbox/Graph/Graph___.000
    ${PROJECT_SOURCE_DIR}/Appl/Installers/fcinst/Release/FCHELP.000)
endif()
# the files the tests leave, checked by an independent reader of the
# format (not on dos32: the files stay inside the DOS machine)
if(Python3_Interpreter_FOUND AND NOT GEOS_HOST STREQUAL "dos32")
  set(_vmcheck ${Python3_EXECUTABLE}
               ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py)
  set_tests_properties(kernel.vm PROPERTIES FIXTURES_SETUP vmfiles)
  add_test(NAME kernel.vm.format COMMAND ${_vmcheck} --strict
    ${GEOS_TESTS_DIR}/GVTOP/A.VM ${GEOS_TESTS_DIR}/GVTOP/B.VM
    ${GEOS_TESTS_DIR}/GVTOP/LM.VM ${GEOS_TESTS_DIR}/GVTOP/HA.VM
    ${GEOS_TESTS_DIR}/GVTOP/BK.VM ${GEOS_TESTS_DIR}/GVTOP/BK2.VM)
  add_test(NAME kernel.vm.format16 COMMAND ${_vmcheck}
    ${GEOS_TESTS_DIR}/GVTOP/REF.VM)
  set_tests_properties(kernel.vm.format kernel.vm.format16
    PROPERTIES FIXTURES_REQUIRED vmfiles)
  set_tests_properties(kernel.db PROPERTIES FIXTURES_SETUP dbfiles)
  add_test(NAME kernel.db.format COMMAND ${_vmcheck} --strict
    ${GEOS_TESTS_DIR}/GDTOP/DB.VM ${GEOS_TESTS_DIR}/GDTOP/DB2.VM)
  # the DOS16 files, changed (their unassigned entries are not cleared)
  add_test(NAME kernel.db.format16 COMMAND ${_vmcheck}
    ${GEOS_TESTS_DIR}/GDTOP/D16_1.VM ${GEOS_TESTS_DIR}/GDTOP/D16_2.VM)
  set_tests_properties(kernel.db.format kernel.db.format16
    PROPERTIES FIXTURES_REQUIRED dbfiles)
  # GEOS trees between DOS names and long names (Driver/IFS/HostFS)
  add_test(NAME tools.geostree COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/geostree.py selftest)
endif()

if(NOT GEOS_HAVE_GOC)
  return()
endif()
# messages through the C kernel and its object system, with classes and
# objects of goc -cg (../Goc)
foreach(_t msg obj)
  geos_goc(_c ../Goc/k${_t}.gp ../Goc/k${_t}.goc)
  geos_test_program(test_kernel_${_t} SOURCES ${_c} DEFINITIONS GEOS_PORT32
                    INCLUDES ${PROJECT_SOURCE_DIR}/CInclude)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_kernel_${_t} PRIVATE -Wno-unused-parameter)
  endif()
  geos_add_test(kernel.${_t} test_kernel_${_t})
endforeach()

if(NOT GEOS_BUILD_GEODES)
  return()
endif()
# the C kernel loads geodes: the process kgapp, its library kglib and a
# driver (../Geode)
geos_test_program(test_kernel_geode EMBED kglib kgapp kgdrv)
geos_add_test(kernel.geode test_kernel_geode)
# starting the kernel: drives, [paths], the UI and a process from files
geos_test_program(test_kernel_boot EMBED kglib kgapp)
geos_add_test(kernel.boot test_kernel_boot)

# drawing through the window system to the host video driver, the memory
# video driver, Berkeley, the TrueType font driver with Cooper Black
geos_test_program(test_kernel_draw
                  EMBED hostvid vidmem berkeley truetype cooper
                  INCLUDES ${PROJECT_SOURCE_DIR}/Library/Kernel)
geos_add_test(kernel.draw test_kernel_draw DISPLAY)
# the bitmaps it leaves (a huge bitmap, one of GrCreateBitmap)
if(Python3_Interpreter_FOUND AND NOT GEOS_HOST STREQUAL "dos32")
  set_tests_properties(kernel.draw PROPERTIES FIXTURES_SETUP bmfiles)
  add_test(NAME kernel.draw.format COMMAND ${_vmcheck} --strict
    ${GEOS_TESTS_DIR}/GDRTOP/HB.VM ${GEOS_TESTS_DIR}/GDRTOP/BM.VM)
  set_tests_properties(kernel.draw.format
    PROPERTIES FIXTURES_REQUIRED bmfiles)
endif()
