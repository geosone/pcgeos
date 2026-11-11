##############################################################################
#
# kgdrv.gp -- a driver geode for the kernel.geode test (Port32/Tests):
# its C strategy routine counts DR_INIT and DR_EXIT.
#
##############################################################################
name     kgdrv.drvr
longname "Kernel Test Driver"
tokenchars "KGDR"
tokenid    0
type   driver, system, single
library geos
export KGDrvGetCounts
