# The host sound driver (hostsnd.geo), on ghal_audio, with the Sound
# Blaster driver's instruments (sbigen, Tools/sbigen).
set(hs_inst ${CMAKE_CURRENT_BINARY_DIR}/hostInstruments.c)
set(hs_asm ${PROJECT_SOURCE_DIR}/Driver/Sound/SoundBlaster/soundblasterVoice.asm)
add_custom_command(OUTPUT ${hs_inst}
  COMMAND ${GEOS_SBIGEN} -o ${hs_inst} ${hs_asm}
  DEPENDS ${hs_asm} ${GEOS_SBIGEN} geos_host_tools
  COMMENT "sbigen hostsnd"
  VERBATIM)
geos_geode(hostsnd hostSound.c hostSynth.c ${hs_inst}
           GP hostsnd.gp PROTOCOL 3.0
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                    ${PROJECT_SOURCE_DIR}/CInclude/Internal)
