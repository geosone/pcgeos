# The drivers (Driver/), each loaded by the C kernel.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()
# the C keyboard driver (Driver/Keyboard): US and German maps
geos_test_program(test_kbd EMBED kbd kbde_gr)
geos_add_test(driver.keyboard test_kbd)

# the C host mouse driver (Driver/Mouse) through the input manager
geos_test_program(test_mouse EMBED hostmse)
geos_add_test(driver.mouse test_mouse)

# the C host power driver (Driver/Power) on GAPI, the notifications
geos_test_program(test_power EMBED hostpwr)
geos_add_test(driver.power test_power)

# the stream driver (Driver/Stream/stream.c): buffers, notifiers, threads
geos_test_program(test_stream EMBED stream)
geos_add_test(driver.stream test_stream)

# the C host video driver (Driver/Video) on ghal_display
geos_test_program(test_video EMBED hostvid)
geos_add_test(driver.video test_video DISPLAY)

# the CUPS port driver (Driver/Stream/Cups) against a stand-in for CUPS
if(GEOS_HOST STREQUAL "linux")
  geos_test_program(test_cups EMBED cups
                    SOURCES ${GEOS_TESTS_SRC}/Common/fakecups.c
                    INCLUDES ${GEOS_TESTS_SRC}/Common
                             ${PROJECT_SOURCE_DIR}/Driver/Stream/Cups)
  geos_add_test(driver.cups test_cups)
endif()

# the PostScript printer driver (Driver/Printer/PScript, PrintCom) with
# the translation library
geos_test_program(test_pscript EMBED eps pscript
                  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude/Internal)
geos_add_test(driver.pscript test_pscript)
# (the other printer drivers print through the spooler: ../Library)

# the host sound driver (Driver/Sound/Host) under the sound library,
# heard through the tap of ghal_audio "null"
if(GEOS_HOST STREQUAL "linux")
  geos_test_program(test_hostsnd EMBED sound stream hostsnd LIBS geos_kernel m
    INCLUDES ${PROJECT_SOURCE_DIR}/CInclude/Internal)
  geos_add_test(driver.hostsnd test_hostsnd)
endif()
