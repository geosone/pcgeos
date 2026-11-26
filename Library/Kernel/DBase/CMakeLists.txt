# The C kernel, DBase/: the database manager.
# Beside the asm they replace:
#   dbaseGroup.c, dbaseStructure.c, dbaseItemBlock.c,
# The files of dbase*.asm.
target_sources(geos_kernel_objs PRIVATE
  dbaseGroup.c
  dbaseMapBlk.c
  dbaseStructure.c
  dbaseItemBlock.c
  dbaseCode.c)
