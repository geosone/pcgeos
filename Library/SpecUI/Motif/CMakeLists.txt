# Motif (motif.geo): the code of CommonUI and its own, in C beside the
# asm.  motif32.gp has the parameters; the 16-bit build keeps motif.gp.
add_subdirectory(Win)
add_subdirectory(View)
geos_get_sources(_motif_goc motif_goc)
geos_geode(motif GP motif32.gp GOC ${_motif_goc}
           INCLUDES ${PROJECT_SOURCE_DIR}/Library/SpecUI/CommonUI
           PROTOCOL 727.0)
