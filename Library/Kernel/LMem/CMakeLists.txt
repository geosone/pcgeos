# The C kernel, LMem/: local memory heaps, the arrays, GCN lists.
# Beside the asm they replace:
#   lmemCode.c    <- local memory heaps of LMem/lmemCode.asm
#   lmemChunkArray.c, lmemElementArray.c, lmemNameArray.c
#                      <- the arrays of the same names (.asm)
#   lmemGCNList.c <- GCN lists of LMem/lmemGCNList.asm
target_sources(geos_kernel_objs PRIVATE
  lmemCode.c
  lmemChunkArray.c
  lmemC.c
  lmemElementArray.c
  lmemNameArray.c
  lmemGCNList.c
  lmemErrorCheck.c)
