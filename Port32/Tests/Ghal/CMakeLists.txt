# The GHAL (Ghal/).  The host-independent tests run on every host, dos32
# included.
geos_test_program(test_ghal_core LIBS ghal)
geos_add_test(ghal.core test_ghal_core)
geos_test_program(test_ghal_fs LIBS ghal)
geos_add_test(ghal.fs test_ghal_fs)
geos_test_program(test_ghal_input LIBS ghal)
geos_add_test(ghal.input test_ghal_input)
geos_test_program(test_ghal_display LIBS ghal)
if(GHAL_HAVE_X11 AND GEOS_LINUX_DISPLAY STREQUAL "x11")
  target_compile_definitions(test_ghal_display PRIVATE GHAL_TEST_X11)
endif()
geos_add_test(ghal.display test_ghal_display DISPLAY)

# ghal_module: a geode in the host's module format (ELF, or DXE3 on
# dos32), embedded in the test program (testgeode of ../Geode).  Not with
# Open Watcom (no geode toolchain).
if(GEOS_HAVE_GOC)
  geos_test_embed(testgeode ${testgeode_GEO} testgeode_image)
  geos_test_program(test_ghal_module LIBS ghal EMBED testgeode)
  geos_add_test(ghal.module test_ghal_module)
endif()

if(GEOS_HOST STREQUAL "linux")
  geos_test_program(test_ghal_posix LIBS ghal)
  geos_add_test(ghal.posix test_ghal_posix)
endif()
if(GEOS_HOST STREQUAL "dos32")
  geos_test_program(test_ghal_dos32 LIBS ghal)
  geos_add_test(ghal.dos32 test_ghal_dos32)
endif()
