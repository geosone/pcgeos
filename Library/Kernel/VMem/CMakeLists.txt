# The C kernel, VMem/: VM files.
# Beside the asm they replace:
#   vmemBlkManip.c, vmemHeader.c, vmemHigh.c, vmemChain.c <- the
#                      same asm files (vmemHigh.c also vmemC.asm): VM
#                      files in the DOS16 format
target_sources(geos_kernel_objs PRIVATE
  vmemBlkManip.c
  vmemLow.c
  vmemHeader.c
  vmemHigh.c
  vmemKernelHigh.c
  vmemChain.c
  vmemHugeArray.c)
