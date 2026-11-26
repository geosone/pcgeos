# The C kernel, Thread/: threads, the scheduler, semaphores.
# Beside the asm they replace:
#   threadThread.c <- threads, scheduler of Thread/threadThread.asm
#   threadSem.c <- semaphores, thread locks of Thread/threadSem.asm
#   threadPrivate.c <- thread-private words of threadPrivate.asm
target_sources(geos_kernel_objs PRIVATE
  threadThread.c
  threadSem.c
  threadPrivate.c
  threadErrorCheck.c)
