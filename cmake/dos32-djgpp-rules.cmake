# Loaded by CMake after the platform files (CMAKE_USER_MAKE_RULES_OVERRIDE
# of toolchain-dos32-djgpp.cmake): link, then bind CWSDPMI into the
# executable (Port32/tools/djgpp-bind.sh).
if(GEOS_DJGPP_EXE2COFF AND GEOS_DJGPP_STUB)
  set(_bind "${CMAKE_CURRENT_LIST_DIR}/../Port32/tools/djgpp-bind.sh ${GEOS_DJGPP_EXE2COFF} ${GEOS_DJGPP_STUB} <TARGET>")
  set(CMAKE_C_LINK_EXECUTABLE
      "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES> && ${_bind}")
  set(CMAKE_CXX_LINK_EXECUTABLE
      "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES> && ${_bind}")
elseif(NOT CMAKE_IN_TRY_COMPILE)
  message(WARNING "cwsdstub.exe or ${DJGPP_TARGET}-exe2coff not found: "
          "the DOS programs need CWSDPMI.EXE next to them or in PATH "
          "(dev-embedded/cwsdpmi installs the stub)")
endif()
