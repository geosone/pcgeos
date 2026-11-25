# The pictures of Library/IDialC (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert.bat
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.idialc checks that both are the
# same.
include(GeosCvtpcx)

# convert.bat
geos_cvtpcx(mkrAUIOfflineC8.ui A035-8.pcx -f -t -x0 -y0 -w34 -h9
            -nAUIOfflineC8)
geos_cvtpcx(mkrAUIOnlineC8.ui A036-8.pcx -f -t -x0 -y0 -w34 -h9
            -nAUIOnlineC8)
geos_cvtpcx(mkrAUIOnlineFlashC8.ui A036a-8.pcx -f -t -x0 -y0 -w34 -h9
            -nAUIOnlineFlashC8)
geos_cvtpcx(mkrCUIConnectC8.ui C033-8.pcx -f -t -x0 -y0 -w80 -h10
            -nCUIConnectC8)
geos_cvtpcx(mkrCUIDisconnectC8.ui C034-8.pcx -f -t -x0 -y0 -w80 -h10
            -nCUIDisconnectC8)
geos_cvtpcx(mkrCUIDisconnectFlashC8.ui C034a-8.pcx -f -t -x0 -y0 -w80 -h10
            -nCUIDisconnectFlashC8)

geos_art(idialc)
