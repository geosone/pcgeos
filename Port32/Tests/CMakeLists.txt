# Host-side tests for the C ports and the GHAL (plan phase P1).
add_executable(test_ffa test_ffa.c)
target_link_libraries(test_ffa PRIVATE geos_math_float)
add_test(NAME math.floatFloatToAscii COMMAND test_ffa)

# Host-independent GHAL tests: run on every host, dos32 included.
add_executable(test_ghal_core test_ghal_core.c)
target_link_libraries(test_ghal_core PRIVATE ghal)
add_test(NAME ghal.core COMMAND test_ghal_core)
add_executable(test_ghal_fs test_ghal_fs.c)
target_link_libraries(test_ghal_fs PRIVATE ghal)
add_test(NAME ghal.fs COMMAND test_ghal_fs)
add_executable(test_ghal_display test_ghal_display.c)
target_link_libraries(test_ghal_display PRIVATE ghal)
if(GHAL_HAVE_X11 AND GEOS_LINUX_DISPLAY STREQUAL "x11")
  target_compile_definitions(test_ghal_display PRIVATE GHAL_TEST_X11)
endif()
add_executable(test_ghal_input test_ghal_input.c)
target_link_libraries(test_ghal_input PRIVATE ghal)
add_test(NAME ghal.input COMMAND test_ghal_input)
# on a screen of its own (Xvfb) where the display is X11; never the desktop
find_program(GEOS_XVFB Xvfb)
if(GEOS_HOST STREQUAL "linux" AND GHAL_HAVE_X11 AND GEOS_LINUX_DISPLAY STREQUAL "x11" AND GEOS_XVFB)
  add_test(NAME ghal.display
           COMMAND ${PROJECT_SOURCE_DIR}/Port32/tools/geos-xserver.sh
                   $<TARGET_FILE:test_ghal_display>)
else()
  add_test(NAME ghal.display COMMAND test_ghal_display)
endif()

# C kernel (Library/Kernel, P4): handle table, memory blocks, threads
add_executable(test_kernel_heap test_kernel_heap.c)
target_link_libraries(test_kernel_heap PRIVATE geos_kernel)
add_test(NAME kernel.heap COMMAND test_kernel_heap)
add_executable(test_kernel_thread test_kernel_thread.c)
target_link_libraries(test_kernel_thread PRIVATE geos_kernel)
add_test(NAME kernel.thread COMMAND test_kernel_thread)
add_executable(test_kernel_lmem test_kernel_lmem.c)
target_link_libraries(test_kernel_lmem PRIVATE geos_kernel)
add_test(NAME kernel.lmem COMMAND test_kernel_lmem)
add_executable(test_kernel_region test_kernel_region.c)
target_link_libraries(test_kernel_region PRIVATE geos_kernel)
add_test(NAME kernel.region COMMAND test_kernel_region)
add_executable(test_kernel_gstate test_kernel_gstate.c)
target_link_libraries(test_kernel_gstate PRIVATE geos_kernel)
add_test(NAME kernel.gstate COMMAND test_kernel_gstate)
add_executable(test_kernel_transform test_kernel_transform.c)
target_link_libraries(test_kernel_transform PRIVATE geos_kernel)
add_test(NAME kernel.transform COMMAND test_kernel_transform)
add_executable(test_kernel_win test_kernel_win.c)
target_include_directories(test_kernel_win PRIVATE
  ${PROJECT_SOURCE_DIR}/Library/Kernel)
target_link_libraries(test_kernel_win PRIVATE geos_kernel)
add_test(NAME kernel.win COMMAND test_kernel_win)
add_executable(test_kernel_file test_kernel_file.c)
target_link_libraries(test_kernel_file PRIVATE geos_kernel)
add_test(NAME kernel.file COMMAND test_kernel_file)
add_executable(test_kernel_fileea test_kernel_fileea.c)
target_link_libraries(test_kernel_fileea PRIVATE geos_kernel)
add_test(NAME kernel.file.geos COMMAND test_kernel_fileea)
add_executable(test_kernel_fileenum test_kernel_fileenum.c)
target_link_libraries(test_kernel_fileenum PRIVATE geos_kernel)
add_test(NAME kernel.fileenum COMMAND test_kernel_fileenum)
add_executable(test_kernel_initfile test_kernel_initfile.c)
target_link_libraries(test_kernel_initfile PRIVATE geos_kernel)
add_test(NAME kernel.initfile COMMAND test_kernel_initfile)
add_executable(test_kernel_stdpath test_kernel_stdpath.c)
target_link_libraries(test_kernel_stdpath PRIVATE geos_kernel)
add_test(NAME kernel.stdpath COMMAND test_kernel_stdpath)
add_executable(test_kernel_shutdown test_kernel_shutdown.c)
target_link_libraries(test_kernel_shutdown PRIVATE geos_kernel)
add_test(NAME kernel.shutdown COMMAND test_kernel_shutdown)
add_executable(test_kernel_im test_kernel_im.c)
target_include_directories(test_kernel_im PRIVATE
  ${PROJECT_SOURCE_DIR}/Library/Kernel)
target_link_libraries(test_kernel_im PRIVATE geos_kernel)
add_test(NAME kernel.im COMMAND test_kernel_im)
add_executable(test_kernel_vm test_kernel_vm.c)
target_link_libraries(test_kernel_vm PRIVATE geos_kernel)
add_test(NAME kernel.vm COMMAND test_kernel_vm)
add_executable(test_kernel_db test_kernel_db.c)
target_link_libraries(test_kernel_db PRIVATE geos_kernel)
# files of DOS16 GEOS with databases (not inside the DOS machine)
if(GEOS_HOST STREQUAL "dos32")
  add_test(NAME kernel.db COMMAND test_kernel_db)
else()
  add_test(NAME kernel.db COMMAND test_kernel_db
    ${PROJECT_SOURCE_DIR}/Appl/Breadbox/Graph/Graph___.000
    ${PROJECT_SOURCE_DIR}/Appl/Installers/fcinst/Release/FCHELP.000)
endif()
# the files it leaves, checked by an independent reader of the format
# (not on dos32: the files stay inside the DOS machine)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND NOT GEOS_HOST STREQUAL "dos32")
  set_tests_properties(kernel.vm PROPERTIES FIXTURES_SETUP vmfiles)
  add_test(NAME kernel.vm.format COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py --strict
    ${CMAKE_CURRENT_BINARY_DIR}/GVTOP/A.VM
    ${CMAKE_CURRENT_BINARY_DIR}/GVTOP/B.VM
    ${CMAKE_CURRENT_BINARY_DIR}/GVTOP/LM.VM
    ${CMAKE_CURRENT_BINARY_DIR}/GVTOP/HA.VM)
  add_test(NAME kernel.vm.format16 COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py
    ${CMAKE_CURRENT_BINARY_DIR}/GVTOP/REF.VM)
  set_tests_properties(kernel.vm.format kernel.vm.format16
    PROPERTIES FIXTURES_REQUIRED vmfiles)
  set_tests_properties(kernel.db PROPERTIES FIXTURES_SETUP dbfiles)
  add_test(NAME kernel.db.format COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py --strict
    ${CMAKE_CURRENT_BINARY_DIR}/GDTOP/DB.VM
    ${CMAKE_CURRENT_BINARY_DIR}/GDTOP/DB2.VM)
  # the DOS16 files, changed (their unassigned entries are not cleared)
  add_test(NAME kernel.db.format16 COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py
    ${CMAKE_CURRENT_BINARY_DIR}/GDTOP/D16_1.VM
    ${CMAKE_CURRENT_BINARY_DIR}/GDTOP/D16_2.VM)
  set_tests_properties(kernel.db.format kernel.db.format16 PROPERTIES FIXTURES_REQUIRED dbfiles)
  # GEOS trees between DOS names and long names (Driver/IFS/HostFS)
  add_test(NAME tools.geostree COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/Port32/tools/geostree.py selftest)
endif()
add_executable(test_kernel_carray test_kernel_carray.c)
target_link_libraries(test_kernel_carray PRIVATE geos_kernel)
add_test(NAME kernel.carray COMMAND test_kernel_carray)

add_executable(test_kernel_local test_kernel_local.c)
target_link_libraries(test_kernel_local PRIVATE geos_kernel)
add_test(NAME kernel.local COMMAND test_kernel_local)
add_executable(test_kernel_timer test_kernel_timer.c)
target_link_libraries(test_kernel_timer PRIVATE geos_kernel)
add_test(NAME kernel.timer COMMAND test_kernel_timer)
add_executable(test_gapi test_gapi.c)
target_link_libraries(test_gapi PRIVATE gapi)
add_test(NAME gapi.time COMMAND test_gapi)
if(GEOS_HOST STREQUAL "linux")
  add_executable(test_gapi_dbus test_gapi_dbus.c)
  target_link_libraries(test_gapi_dbus PRIVATE gapi)
  add_test(NAME gapi.dbus COMMAND test_gapi_dbus)
  add_executable(test_gapi_print test_gapi_print.c fakecups.c)
  target_link_libraries(test_gapi_print PRIVATE gapi)
  add_test(NAME gapi.print COMMAND test_gapi_print)
endif()
add_executable(test_kernel_sys test_kernel_sys.c)
target_link_libraries(test_kernel_sys PRIVATE geos_kernel)
add_test(NAME kernel.sys COMMAND test_kernel_sys)

# ghal_module: a geode in the host's module format (ELF, or DXE3 on dos32),
# embedded in the test program.  Not with Open Watcom (no geode toolchain).
if(NOT CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)
  geos_geode(testgeode Geode/testgeode.c)
  geos_embed(_testgeode_c ${testgeode_GEO} testgeode_image)
  add_executable(test_ghal_module test_ghal_module.c ${_testgeode_c})
  add_dependencies(test_ghal_module geode_testgeode)
  target_link_libraries(test_ghal_module PRIVATE ghal)
  add_test(NAME ghal.module COMMAND test_ghal_module)
endif()

if(GEOS_HOST STREQUAL "linux")
  add_executable(test_ghal_posix test_ghal_posix.c)
  target_link_libraries(test_ghal_posix PRIVATE ghal)
  add_test(NAME ghal.posix COMMAND test_ghal_posix)
endif()

if(GEOS_HOST STREQUAL "dos32")
  add_executable(test_ghal_dos32 test_ghal_dos32.c)
  target_link_libraries(test_ghal_dos32 PRIVATE ghal)
  add_test(NAME ghal.dos32 COMMAND test_ghal_dos32)
endif()

# The expected strings of test_ffa must be those of the original GEOS math
# library (reference output in GeosRef/ffaref.txt, see GeosRef/README.md).
# Runs on the build host, also when cross compiling.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME math.floatFloatToAscii.geosref
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/check_ref.py
                   ${CMAKE_CURRENT_SOURCE_DIR}/ffa_cases.h
                   ${CMAKE_CURRENT_SOURCE_DIR}/test_ffa.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/GeosRef/ffaref.txt)
endif()

# Base types of GEOS in C (geosport.h through geos.h), on every host.
add_executable(test_geosport test_geosport.c)
target_compile_definitions(test_geosport PRIVATE GEOS_PORT32)
target_include_directories(test_geosport PRIVATE
  ${PROJECT_SOURCE_DIR}/CInclude)
add_test(NAME cinclude.geosport COMMAND test_geosport)

# Every header of cinclude_port32.txt must compile on its own (after
# geos.h) under GEOS_PORT32 without warnings: one translation unit each.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/cinclude_port32.txt _hdrs
       REGEX "^[^#]")
  set(_tus)
  foreach(_h IN LISTS _hdrs)
    string(MAKE_C_IDENTIFIER "${_h}" _id)
    set(_tu ${CMAKE_CURRENT_BINARY_DIR}/cinclude/${_id}.c)
    file(CONFIGURE OUTPUT ${_tu}
         CONTENT "#include <geos.h>\n#include <${_h}>\n" @ONLY)
    list(APPEND _tus ${_tu})
  endforeach()
  add_library(cinclude_port32 OBJECT ${_tus})
  target_compile_definitions(cinclude_port32 PRIVATE GEOS_PORT32)
  target_compile_options(cinclude_port32 PRIVATE -Werror)
  target_include_directories(cinclude_port32 PRIVATE
    ${PROJECT_SOURCE_DIR}/CInclude)
endif()

# goc -cg: object resources, class structures and chunk handles of a .goc
# geode in two files, linked and checked on the host (Goc/gocres*.goc).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
# goc -cg needs C99 compound literals, which Open Watcom lacks: dos32 is
# built with DJGPP gcc for the port (decided 2026-11-11, ABI.md section 5).
if(GEOS_GOC AND Python3_Interpreter_FOUND AND NOT CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  geos_goc(_gocres_c Goc/gocres.gp Goc/gocres.goc Goc/gocres2.goc)
  add_executable(test_gocres ${_gocres_c} Goc/test_gocres.c Goc/gocres_lmem.c)
  target_link_libraries(test_gocres PRIVATE geos_kernel)
  # test_gocres.c stands in for the message kernel (fake handles show
  # where goc sends to): its entry points get their own names, so the
  # real ones of geos_kernel (used for LMem) do not clash
  target_compile_definitions(test_gocres PRIVATE GEOS_PORT32
    CObjMessageP=GocTestObjMessageP CObjCallSuperP=GocTestObjCallSuperP
    CMessageDispatchP=GocTestMessageDispatchP)
  target_include_directories(test_gocres PRIVATE
    ${PROJECT_SOURCE_DIR}/CInclude)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # parameters of methods are often unused
    target_compile_options(test_gocres PRIVATE -Wno-unused-parameter)
  endif()
  add_test(NAME goc.resources COMMAND test_gocres)

  # messages through the C kernel, with classes and objects of goc -cg
  geos_goc(_kmsg_c Goc/kmsg.gp Goc/kmsg.goc)
  add_executable(test_kernel_msg ${_kmsg_c} test_kernel_msg.c)
  target_link_libraries(test_kernel_msg PRIVATE geos_kernel)
  target_compile_definitions(test_kernel_msg PRIVATE GEOS_PORT32)
  target_include_directories(test_kernel_msg PRIVATE
    ${PROJECT_SOURCE_DIR}/CInclude)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_kernel_msg PRIVATE -Wno-unused-parameter)
  endif()
  add_test(NAME kernel.msg COMMAND test_kernel_msg)

  # the object system of the C kernel, with classes and objects of goc -cg
  geos_goc(_kobj_c Goc/kobj.gp Goc/kobj.goc)
  add_executable(test_kernel_obj ${_kobj_c} test_kernel_obj.c)
  target_link_libraries(test_kernel_obj PRIVATE geos_kernel)
  target_compile_definitions(test_kernel_obj PRIVATE GEOS_PORT32)
  target_include_directories(test_kernel_obj PRIVATE
    ${PROJECT_SOURCE_DIR}/CInclude)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_kernel_obj PRIVATE -Wno-unused-parameter)
  endif()
  add_test(NAME kernel.obj COMMAND test_kernel_obj)
endif()

# The SDK_C samples of goc_sdk.txt must keep building through goc -cg,
# each as a geode in the module format of the host (ELF, DXE3 on dos32;
# cmake/GeosGeode.cmake).  Their imports are resolved only when a kernel
# loads them; test geode.hello does that for Hello.  Only errors count
# here; the samples are old code with many harmless warnings.
if(GEOS_GOC AND Python3_Interpreter_FOUND AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/goc_sdk.txt _samples REGEX "^[^#]")
  set(_sdk_geodes)
  foreach(_s IN LISTS _samples)
    set(_dir ${PROJECT_SOURCE_DIR}/Appl/SDK_C/${_s})
    file(GLOB _gp ${_dir}/*.gp)
    file(GLOB _gocs ${_dir}/*.goc)
    list(GET _gp 0 _gp)
    geos_geode(sdk_${_s} GP ${_gp} GOC ${_gocs} OPTIONS -w)
    list(APPEND _sdk_geodes geode_sdk_${_s})
  endforeach()
  add_custom_target(goc_sdk DEPENDS ${_sdk_geodes})

  # a real geode loaded and checked: Hello (no kernel yet: dummy imports)
  geos_embed(_hello_c ${sdk_Hello_GEO} hello_geode)
  add_executable(test_geode_hello test_geode_hello.c geode_hello_check.c
                 ${_hello_c})
  add_dependencies(test_geode_hello geode_sdk_Hello)
  target_link_libraries(test_geode_hello PRIVATE ghal)
  set_source_files_properties(geode_hello_check.c PROPERTIES
    COMPILE_DEFINITIONS GEOS_PORT32
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/CInclude")
  add_test(NAME geode.hello COMMAND test_geode_hello)

  # the C kernel loads geodes: the process kgapp and its library kglib
  geos_geode(kglib GP Geode/kglib.gp GOC Geode/kglib.goc)
  geos_geode(kgapp GP Geode/kgapp.gp GOC Geode/kgapp.goc)
  geos_geode(kgdrv Geode/kgdrv.c GP Geode/kgdrv.gp)
  geos_embed(_kglib_c ${kglib_GEO} kglib_geode)
  geos_embed(_kgapp_c ${kgapp_GEO} kgapp_geode)
  geos_embed(_kgdrv_c ${kgdrv_GEO} kgdrv_geode)
  add_executable(test_kernel_geode test_kernel_geode.c ${_kglib_c}
                 ${_kgapp_c} ${_kgdrv_c})
  add_dependencies(test_kernel_geode geode_kglib geode_kgapp geode_kgdrv)
  target_link_libraries(test_kernel_geode PRIVATE geos_kernel)
  add_test(NAME kernel.geode COMMAND test_kernel_geode)

  # starting the kernel: drives, [paths], the UI and a process from files
  add_executable(test_kernel_boot test_kernel_boot.c ${_kglib_c}
                 ${_kgapp_c})
  add_dependencies(test_kernel_boot geode_kglib geode_kgapp)
  target_link_libraries(test_kernel_boot PRIVATE geos_kernel)
  add_test(NAME kernel.boot COMMAND test_kernel_boot)

  # the C keyboard driver (Driver/Keyboard): US and German maps
  geos_embed(_kbd_c ${kbd_GEO} kbd_geode)
  geos_embed(_kbde_gr_c ${kbde_gr_GEO} kbde_gr_geode)
  add_executable(test_kbd test_kbd.c ${_kbd_c} ${_kbde_gr_c})
  add_dependencies(test_kbd geode_kbd geode_kbde_gr)
  target_link_libraries(test_kbd PRIVATE geos_kernel)
  add_test(NAME driver.keyboard COMMAND test_kbd)

  # the CUPS port driver (Driver/Stream/Cups) against a stand-in for CUPS
  if(GEOS_HOST STREQUAL "linux")
    geos_embed(_cups_c ${cups_GEO} cups_geode)
    add_executable(test_cups test_cups.c fakecups.c ${_cups_c})
    add_dependencies(test_cups geode_cups)
    target_include_directories(test_cups PRIVATE
                               ${PROJECT_SOURCE_DIR}/Driver/Stream/Cups)
    target_link_libraries(test_cups PRIVATE geos_kernel)
    add_test(NAME driver.cups COMMAND test_cups)
  endif()

  # the PostScript translation library (Library/Trans/Graphics/Vector/EPS)
  geos_embed(_eps_c ${eps_GEO} eps_geode)
  add_executable(test_eps test_eps.c ${_eps_c})
  add_dependencies(test_eps geode_eps)
  target_link_libraries(test_eps PRIVATE geos_kernel)
  add_test(NAME library.eps COMMAND test_eps)

  # the PostScript printer driver (Driver/Printer/PScript, PrintCom) with
  # the translation library
  geos_embed(_pscript_c ${pscript_GEO} pscript_geode)
  add_executable(test_pscript test_pscript.c ${_eps_c} ${_pscript_c})
  add_dependencies(test_pscript geode_eps geode_pscript)
  target_include_directories(test_pscript PRIVATE
                             ${PROJECT_SOURCE_DIR}/CInclude/Internal)
  target_link_libraries(test_pscript PRIVATE geos_kernel)
  add_test(NAME driver.pscript COMMAND test_pscript)

  # the print spool library (Library/Spool) printing to files through the
  # file stream driver (Driver/Stream/Filestr) with the PostScript driver
  geos_embed(_spool_c ${spool_GEO} spool_geode)
  geos_embed(_filestr_c ${filestr_GEO} filestr_geode)
  geos_embed(_epson9_c ${epson9_GEO} epson9_geode)
  geos_embed(_epson24_c ${epson24_GEO} epson24_geode)
  geos_embed(_vidmem_c ${vidmem_GEO} vidmem_geode)
  geos_embed(_berkeley_c ${PROJECT_SOURCE_DIR}/FontData/Berkeley.fnt
             berkeley_fnt)
  add_executable(test_spool test_spool.c ${_spool_c} ${_filestr_c}
                 ${_eps_c} ${_pscript_c} ${_epson9_c} ${_epson24_c} ${_vidmem_c}
                 ${_berkeley_c})
  add_dependencies(test_spool geode_spool geode_filestr geode_eps
                   geode_pscript geode_epson9 geode_epson24
                   geode_vidmem)
  target_include_directories(test_spool PRIVATE
                             ${PROJECT_SOURCE_DIR}/CInclude/Internal)
  target_link_libraries(test_spool PRIVATE geos_kernel)
  if(GEOS_HOST STREQUAL "linux")
    # and through the CUPS port driver to the stand-in for CUPS
    target_sources(test_spool PRIVATE fakecups.c ${_cups_c})
    add_dependencies(test_spool geode_cups)
    target_compile_definitions(test_spool PRIVATE GEOS_TEST_CUPS)
  endif()
  add_test(NAME library.spool COMMAND test_spool)

  # the C host mouse driver (Driver/Mouse) through the input manager
  geos_embed(_hostmse_c ${hostmse_GEO} hostmse_geode)
  add_executable(test_mouse test_mouse.c ${_hostmse_c})
  add_dependencies(test_mouse geode_hostmse)
  target_link_libraries(test_mouse PRIVATE geos_kernel)
  add_test(NAME driver.mouse COMMAND test_mouse)

  # the C host video driver (Driver/Video) on ghal_display; where the
  # display is X11, on a screen of its own (Xvfb)
  geos_embed(_hostvid_c ${hostvid_GEO} hostvid_geode)
  add_executable(test_video test_video.c ${_hostvid_c})
  add_dependencies(test_video geode_hostvid)
  target_link_libraries(test_video PRIVATE geos_kernel)
  if(GEOS_HOST STREQUAL "linux" AND GHAL_HAVE_X11 AND
     GEOS_LINUX_DISPLAY STREQUAL "x11" AND GEOS_XVFB)
    add_test(NAME driver.video
             COMMAND ${PROJECT_SOURCE_DIR}/Port32/tools/geos-xserver.sh
                     $<TARGET_FILE:test_video>)
  else()
    add_test(NAME driver.video COMMAND test_video)
  endif()

  # drawing through the window system to the host video driver (vidmem
  # and Berkeley embedded with test_spool)
  # the TrueType font driver (Driver/Font) and a font for it
  geos_embed(_truetype_c ${truetype_GEO} truetype_geode)
  geos_embed(_cooper_c ${PROJECT_SOURCE_DIR}/FontData/Ttf/cooper.ttf
             cooper_ttf)
  add_executable(test_kernel_draw test_kernel_draw.c ${_hostvid_c}
                 ${_vidmem_c} ${_berkeley_c} ${_truetype_c} ${_cooper_c})
  add_dependencies(test_kernel_draw geode_hostvid geode_vidmem
                   geode_truetype)
  target_include_directories(test_kernel_draw PRIVATE
    ${PROJECT_SOURCE_DIR}/Library/Kernel)
  target_link_libraries(test_kernel_draw PRIVATE geos_kernel)
  if(GEOS_HOST STREQUAL "linux" AND GHAL_HAVE_X11 AND
     GEOS_LINUX_DISPLAY STREQUAL "x11" AND GEOS_XVFB)
    add_test(NAME kernel.draw
             COMMAND ${PROJECT_SOURCE_DIR}/Port32/tools/geos-xserver.sh
                     $<TARGET_FILE:test_kernel_draw>)
  else()
    add_test(NAME kernel.draw COMMAND test_kernel_draw)
  endif()
  # the bitmaps it leaves (a huge bitmap, one of GrCreateBitmap)
  if(Python3_FOUND AND NOT GEOS_HOST STREQUAL "dos32")
    set_tests_properties(kernel.draw PROPERTIES FIXTURES_SETUP bmfiles)
    add_test(NAME kernel.draw.format COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/Port32/tools/vmcheck.py --strict
      ${CMAKE_CURRENT_BINARY_DIR}/GDRTOP/HB.VM
      ${CMAKE_CURRENT_BINARY_DIR}/GDRTOP/BM.VM)
    set_tests_properties(kernel.draw.format
      PROPERTIES FIXTURES_REQUIRED bmfiles)
  endif()
endif()
