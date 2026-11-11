# Cross build for GEOS_HOST=dos32 with DJGPP gcc (i586-pc-msdosdjgpp), the
# port's compiler for dos32 (Port32/ABI.md section 5: goc -cg needs C99).
#
#   cmake -S . -B build-dos32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-djgpp.cmake
#   cmake --build build-dos32 && ctest --test-dir build-dos32
#
# The toolchain comes from crossdev (Gentoo):
#   crossdev -t i586-pc-msdosdjgpp --lcat dev-embedded --lpkg djcrx \
#            --kpkg "[none]"
# DJGPP programs need a DPMI host.  Every executable gets CWSDPMI bound in
# (cwsdstub.exe, GEOS_DJGPP_STUB), so it runs on plain DOS like the DOS/32A
# programs of the Watcom build.  Tests run through Port32/tools/dosrun.sh
# (a test harness for the DOS binaries; GEOS itself does not run emulated).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i586)

set(DJGPP_TARGET i586-pc-msdosdjgpp CACHE STRING "DJGPP target triplet")
set(CMAKE_C_COMPILER ${DJGPP_TARGET}-gcc)
set(CMAKE_CXX_COMPILER ${DJGPP_TARGET}-g++)
set(CMAKE_AR ${DJGPP_TARGET}-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${DJGPP_TARGET}-ranlib CACHE FILEPATH "")
set(CMAKE_EXECUTABLE_SUFFIX .exe)
set(CMAKE_EXECUTABLE_SUFFIX_C .exe)

set(GEOS_HOST dos32 CACHE STRING "GHAL host" FORCE)

find_program(GEOS_DJGPP_EXE2COFF ${DJGPP_TARGET}-exe2coff)
find_file(GEOS_DJGPP_STUB cwsdstub.exe
          PATHS /usr/share/cwsdpmi /usr/${DJGPP_TARGET}/bin NO_DEFAULT_PATH)
set(CMAKE_USER_MAKE_RULES_OVERRIDE ${CMAKE_CURRENT_LIST_DIR}/dos32-djgpp-rules.cmake)
set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_LIST_DIR}/../Port32/tools/dosrun.sh)

set(CMAKE_FIND_ROOT_PATH /usr/${DJGPP_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
