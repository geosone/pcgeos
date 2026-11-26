# The sound library (sound.geo) in C, beside the asm (soundLocal.h).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

set(snd ${CMAKE_CURRENT_SOURCE_DIR})
geos_geode(sound ${snd}/soundCommon.c ${snd}/soundResident.c
           ${snd}/soundVoiceAllocation.c ${snd}/soundDriver.c
           ${snd}/soundMixer.c ${snd}/soundC.c
           GP sound32.gp GOC ${snd}/soundUI.goc REV sound.rev
           INCLUDES ${snd})
