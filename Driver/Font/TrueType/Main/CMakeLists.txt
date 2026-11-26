# The TrueType font driver, Main/: the driver's entry, a C file per asm
# file.
geos_sources(truetype mainManager.c truetypeInit.c truetypeWidths.c
  truetypeChars.c truetypeMetrics.c truetypePath.c ansic_runtime.c
  truetypeVariable.c)
