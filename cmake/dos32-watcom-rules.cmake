# Loaded by CMake after Platform/DOS-OpenWatcom.cmake (via
# CMAKE_USER_MAKE_RULES_OVERRIDE): link with the DOS/32A extender instead of
# the platform default DOS/4GW.
foreach(_kind EXE SHARED MODULE)
  string(REPLACE "system dos4g" "system dos32a"
         CMAKE_${_kind}_LINKER_FLAGS_INIT "${CMAKE_${_kind}_LINKER_FLAGS_INIT}")
endforeach()
# Header dependencies: CMake has no depfile rule for Open Watcom; wcc386
# writes make-style ones with -ad (paths with forward slashes: -adhp).
set(CMAKE_DEPFILE_FLAGS_C "-ad=<DEP_FILE> -adt=<OBJECT> -add=<SOURCE> -adhp")
set(CMAKE_C_DEPFILE_FORMAT gcc)
set(CMAKE_C_DEPENDS_USE_COMPILER TRUE)
