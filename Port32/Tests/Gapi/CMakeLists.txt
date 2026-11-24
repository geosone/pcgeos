# GAPI (Gapi/): the clock, and on Linux D-Bus and printing against a
# stand-in for CUPS (../Common/fakecups.c).
geos_test_program(test_gapi LIBS gapi)
geos_add_test(gapi.time test_gapi)
if(GEOS_HOST STREQUAL "linux")
  geos_test_program(test_gapi_dbus LIBS gapi)
  geos_add_test(gapi.dbus test_gapi_dbus)
  geos_test_program(test_gapi_print LIBS gapi
                    SOURCES ${GEOS_TESTS_SRC}/Common/fakecups.c
                    INCLUDES ${GEOS_TESTS_SRC}/Common)
  geos_add_test(gapi.print test_gapi_print)
endif()
