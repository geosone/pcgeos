# same-text.cmake -- cmake -D SRC=<dir> -D GEN=<dir> -D GLOB=<pattern>
#   [-D EXCEPT=<file>;...] [-D LOOSE=1] -P same-text.cmake
# Every file of SRC matching GLOB must have been made in GEN with the
# same text (line ends aside: the files checked in have CR LF), but
# those in EXCEPT (written by hand, not made).  LOOSE: a comment before
# the first "Moniker generated" one, and empty lines, do not count.  For the
# pictures of CommonUI/CArt, made by cvtpcx at build time, against the
# .ui files the 16-bit build uses.
file(GLOB files RELATIVE ${SRC} ${SRC}/${GLOB})
if(EXCEPT)
  list(REMOVE_ITEM files ${EXCEPT})
endif()
set(bad 0)
set(n 0)
foreach(f IN LISTS files)
  math(EXPR n "${n} + 1")
  if(NOT EXISTS ${GEN}/${f})
    message("not made: ${f}")
    math(EXPR bad "${bad} + 1")
    continue()
  endif()
  file(READ ${SRC}/${f} a)
  file(READ ${GEN}/${f} b)
  string(REPLACE "\r" "" a "${a}")
  string(REPLACE "\r" "" b "${b}")
  if(LOOSE)
    # a comment put before cvtpcx's own by hand (the RCS $Id$ of many),
    # and empty lines, do not count
    set(c "/*\n * Moniker generated")
    string(FIND "${a}" "${c}" i)
    string(FIND "${b}" "${c}" j)
    if(i GREATER 0 AND j EQUAL 0)
      string(SUBSTRING "${a}" ${i} -1 a)
    endif()
    string(REGEX REPLACE "\n[ \t]*\n" "\n" a "${a}")
    string(REGEX REPLACE "\n[ \t]*\n" "\n" a "${a}")
    string(REGEX REPLACE "\n[ \t]*\n" "\n" b "${b}")
    string(REGEX REPLACE "\n[ \t]*\n" "\n" b "${b}")
  endif()
  if(NOT a STREQUAL b)
    message("differs: ${f}")
    math(EXPR bad "${bad} + 1")
  endif()
endforeach()
message("${n} files, ${bad} not the same")
if(bad OR n EQUAL 0)
  message(FATAL_ERROR "made files differ")
endif()
