# The pictures of Library/Chart (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): convert in CMake, and cvtpcx calls found for
# those no convert script here makes (it gives the same files).  The .ui
# files checked in are the 16-bit build's; the ctest library.art.chart
# checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrColumn.ui fgcalc.pcx -f -m13 -t -z -x0 -y80 -nColumn)
geos_cvtpcx(mkrBar.ui fgcalc.pcx -f -m13 -t -z -x0 -y96 -nBar)
geos_cvtpcx(mkrLine.ui fgcalc.pcx -f -m13 -t -z -x0 -y112 -nLine)
geos_cvtpcx(mkrArea.ui fgcalc.pcx -f -m13 -t -z -x0 -y128 -nArea)
geos_cvtpcx(mkrScatter.ui fgcalc.pcx -f -m13 -t -z -x0 -y144 -nScatter)
geos_cvtpcx(mkrPie.ui fgcalc.pcx -f -m13 -t -z -x0 -y160 -nPie)
geos_cvtpcx(mkrHighLow.ui fgcalc.pcx -f -m13 -t -z -x0 -y208 -nHighLow)
geos_cvtpcx(mkrSpider.ui fgcalc.pcx -f -m13 -t -z -x0 -y224 -nSpider)

# not in a convert script: found
geos_cvtpcx(mkrAreaTC.ui A142-8.pcx -2 -nAreaTC -m225 -x0 -y0 -w17 -h16
            -Stool -stiny)
geos_cvtpcx(mkrBarTC.ui A140-8.pcx -2 -nBarTC -m225 -x0 -y0 -w17 -h16 -Stool
            -stiny)
geos_cvtpcx(mkrColumnTC.ui A139-8.pcx -2 -nColumnTC -m225 -x0 -y0 -w17 -h16
            -Stool -stiny)
geos_cvtpcx(mkrHighLowTC.ui A145-8.pcx -2 -nHighLowTC -m225 -x0 -y0 -w17
            -h16 -Stool -stiny)
geos_cvtpcx(mkrLineTC.ui A141-8.pcx -2 -nLineTC -m225 -x0 -y0 -w17 -h16
            -Stool -stiny)
geos_cvtpcx(mkrPieTC.ui A144-8.pcx -2 -nPieTC -m225 -x0 -y0 -w17 -h16 -Stool
            -stiny)
geos_cvtpcx(mkrScatterTC.ui A143-8.pcx -2 -nScatterTC -m225 -x0 -y0 -w17
            -h16 -Stool -stiny)

geos_art(chart)
