# The spooler, Process/: the queues and their threads, the ports, the
# application object.
geos_sources(spool
  processLockQueue.c processUtils.c processQueue.c processLoop.c
  processThread.c processPDL.c processPort.c processFile.c
  processCustom.c processNothing.c processError.c processRestart.c
  processGraphics.c processText.c)
geos_sources(spool_goc processApp.goc)
