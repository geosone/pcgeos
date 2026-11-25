# The libraries (Library/): the UI, the specific UI, text, the PostScript
# translation library, the spooler with the printer drivers.  The geodes
# of the tests are in ../Geode.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()
# the visual classes of the UI library (Library/User/Vis) in a window
geos_test_program(test_ui_vis EMBED ui uivis)
geos_add_test(library.ui.vis test_ui_vis)

# the text library (Library/Text): text objects off the screen
geos_test_program(test_text EMBED ui text textobj berkeley)
geos_add_test(library.text test_text)

# the flow object between the input manager and two geodes
geos_test_program(test_ui_flow EMBED ui uiflow)
geos_add_test(library.ui.flow test_ui_flow)

# the generic classes, built by a specific UI library of the test
geos_test_program(test_ui_gen EMBED ui uigen tspui)
geos_add_test(library.ui.gen test_ui_gen)

# the system, screen and field classes, on the memory video driver
geos_test_program(test_ui_system EMBED ui uisys tspui vidmem)
geos_add_test(library.ui.system test_ui_system)

# an application's life: GenProcessClass, GenApplicationClass
geos_test_program(test_ui_app EMBED ui uahost uiapp tspui vidmem)
geos_add_test(library.ui.app test_ui_app)

# the Motif specific UI (Library/SpecUI) on the host video driver: the
# system, a screen and a field
geos_test_program(test_ui_motif EMBED ui uimotif motif hostvid text)
geos_add_test(library.ui.motif test_ui_motif DISPLAY)

# the UI booted with the Motif specific UI and an application of
# "[ui] execOnStartup" with a document window
geos_test_program(test_ui_boot
                  EMBED ui uiboot motif hostvid vidmem berkeley text
                        sound stream tsnddrv
                  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude/Internal
                           ${GEOS_TESTS_SRC}/Geode)
geos_add_test(library.ui.boot test_ui_boot DISPLAY)
# the same with the menu bar in the header of a maximized window
# (windowOptions UIWO_COMBINE_HEADER_AND_MENU_IN_MAXIMIZED_WINDOWS)
geos_add_test(library.ui.boot.menuhdr test_ui_boot DISPLAY menuhdr)

# the PostScript translation library (Library/Trans/Graphics/Vector/EPS),
# with Berkeley for the metrics of its text; the TrueType font driver
# (Driver/Font) and a font for it: Cooper Black, which no printer has,
# downloaded as a Type 3 font
geos_test_program(test_eps EMBED eps berkeley truetype cooper)
geos_add_test(library.eps test_eps)

# the print spool library (Library/Spool) printing to files through the
# file stream driver (Driver/Stream/Filestr) with the printer drivers
set(_dotmatrix epmx9 eplx9 eprx9 bchip9 diconix9 star9 grpr9 nec24 epshi24
    epson48 prop9 propx24 ppds24 canon48 oki9 citoh9 tosh24)
# the color DeskJets (Driver/Printer/HP/Dj500c, Mercury): CMY, KCMY;
# the Canon BJC (Driver/Printer/DotMatrix/CanonBJC), the QuietJet,
# the Canon CaPSL lasers (Driver/Printer/HP/Lbp), the text only ones
# (Driver/Printer/Ascii)
set(_color dj500c mercury canonbjc quietjet lbp dumb diablo)
geos_test_program(test_spool
  EMBED spool filestr eps pscript epson9 epson24 deskjet pcl4
        ${_dotmatrix} ${_color} vidmem berkeley truetype cooper
  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude/Internal
           ${GEOS_TESTS_SRC}/Common)
if(GEOS_HOST STREQUAL "linux")
  # and through the CUPS port driver to the stand-in for CUPS
  target_sources(test_spool PRIVATE ${GEOS_TESTS_SRC}/Common/fakecups.c)
  geos_test_embed(cups)
  target_link_libraries(test_spool PRIVATE embed_cups)
  target_compile_definitions(test_spool PRIVATE GEOS_TEST_CUPS)
endif()
geos_add_test(library.spool test_spool)
# the DeskJet driver (Driver/Printer/HP/Deskjet) through the spooler
geos_add_test(driver.deskjet test_spool deskjet)
foreach(_d ${_color})
  geos_add_test(driver.${_d} test_spool color ${_d})
endforeach()
# the PCL 4 driver (Driver/Printer/HP/Pcl4): rasters, soft fonts
geos_add_test(driver.pcl4 test_spool pcl4)
# the other Epson style drivers (Driver/Printer/DotMatrix), a test each
foreach(_d ${_dotmatrix})
  geos_add_test(driver.${_d} test_spool dotmatrix ${_d})
endforeach()

# the sound library (Library/Sound) on the stream driver with a sound
# driver that logs what it is told (../Geode/tsnddrv.c)
geos_test_program(test_sound EMBED sound stream tsnddrv
  INCLUDES ${PROJECT_SOURCE_DIR}/CInclude/Internal ${GEOS_TESTS_SRC}/Geode)
geos_add_test(library.sound test_sound)
