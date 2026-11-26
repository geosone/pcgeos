# The spooler, Lib/: the library's routines (printers, paper sizes,
# drivers).  The tables of libTables.asm and lib*Tab.asm are made by
# ../CMakeLists.txt.
geos_sources(spool
  libPrinter.c libMisc.c libHost.c libPaper.c libDriver.c libInit.c)
