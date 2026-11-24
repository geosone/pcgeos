# The C kernel, Disk/: disks.
# Beside the asm they replace:
#   diskHigh.c <- disks of Disk/diskHigh.asm, diskC.asm
target_sources(geos_kernel_objs PRIVATE
  diskHigh.c)
