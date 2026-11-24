# The C kernel, Local/: the localization code.
# The files of the same asm files (SBCS);
#                      their tables (stringCharTables.asm, ...,
#                      cmap*.asm) and strings (localStrings.asm) become
#                      localTables.c and localStrings.h at build time
#                      (Port32/tools/localgen.py)
target_sources(geos_kernel_objs PRIVATE
  stringChar.c
  stringCase.c
  stringSize.c
  stringSort.c
  dosConvert.c
  localStrings.c
  stringQuotes.c
  numericFormats.c
  numericMeasure.c
  dateTimeFormat.c
  dateTimeFields.c
  dateTimeParse.c
  localInit.c
  ${CMAKE_CURRENT_BINARY_DIR}/localTables.c)

# The tables of the localization code, from the asm
set(_local_tables
  stringCharTables.asm stringCaseTables.asm stringSortTables.asm
  cmapUS.asm cmapMulti.asm cmapFrench.asm cmapNordic.asm cmapPort.asm
  cmapLatin1.asm localStrings.asm)
list(TRANSFORM _local_tables PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/localTables.c
                          ${CMAKE_CURRENT_BINARY_DIR}/localStrings.h
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/localgen.py
          -o ${CMAKE_CURRENT_BINARY_DIR}/localTables.c
          --header ${CMAKE_CURRENT_BINARY_DIR}/localStrings.h
          --chars ${PROJECT_SOURCE_DIR}/Include/char.def
          --lexical ${PROJECT_SOURCE_DIR}/Include/Internal/lexical.def
          ${_local_tables}
  DEPENDS ${_local_tables} ${PROJECT_SOURCE_DIR}/Include/char.def
          ${PROJECT_SOURCE_DIR}/Include/Internal/lexical.def
          ${PROJECT_SOURCE_DIR}/Port32/tools/localgen.py
  COMMENT "localization tables"
  VERBATIM)
# the kernel is a target of the directory above: it waits for the tables
# through a target of this one
add_custom_target(geos_local_tables
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/localTables.c
          ${CMAKE_CURRENT_BINARY_DIR}/localStrings.h)
add_dependencies(geos_kernel_objs geos_local_tables)
# (localStrings.h)
target_include_directories(geos_kernel_objs PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
