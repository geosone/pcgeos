# The C kernel, Heap/: the heap: handles and memory blocks.
# Beside the asm they replace:
#   heapHandle.c  <- handle table of Heap/heapHandle.asm
#   heapHigh.c    <- memory block API of Heap/heapHigh.asm, heapC.asm
#   heapCompress.c <- LZG of Heap/heapCompress.asm
target_sources(geos_kernel_objs PRIVATE
  heapHandle.c
  heapHigh.c
  heapErrorCheck.c
  heapCompress.c)
