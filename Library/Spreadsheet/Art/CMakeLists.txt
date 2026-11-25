# The pictures of Library/Spreadsheet (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): convert in CMake, and cvtpcx calls found for
# those no convert script here makes (it gives the same files).  The .ui
# files checked in are the 16-bit build's; the ctest
# library.art.spreadsheet checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrInsertRow.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y240
            -nInsertRow)
geos_cvtpcx(mkrInsertColumn.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y256
            -nInsertColumn)
geos_cvtpcx(mkrDeleteRow.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y272
            -nDeleteRow)
geos_cvtpcx(mkrDeleteColumn.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y288
            -nDeleteColumn)
geos_cvtpcx(mkrSortAscend.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y304
            -nSortAscend)
geos_cvtpcx(mkrSortDescend.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y320
            -nSortDescend)
geos_cvtpcx(mkrRecalc.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y368 -nRecalc)
geos_cvtpcx(mkrBorderOutline.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y336
            -nBorderOutline)
geos_cvtpcx(mkrBorderAll.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y352
            -nBorderAll)
geos_cvtpcx(mkrColumn.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y80 -nColumn)
geos_cvtpcx(mkrBar.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y96 -nBar)
geos_cvtpcx(mkrLine.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y112 -nLine)
geos_cvtpcx(mkrArea.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y128 -nArea)
geos_cvtpcx(mkrScatter.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y144
            -nScatter)
geos_cvtpcx(mkrPie.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y160 -nPie)
geos_cvtpcx(mkrHighLow.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y208
            -nHighLow)
geos_cvtpcx(mkrSpider.ui PCX/fgcalc.pcx -2 -f -m13 -t -z -x0 -y224 -nSpider)
geos_cvtpcx(mkrBackspace.ui PCX/clcbut01.pcx -2 -f -m13 -t -b -anormal -x0
            -y0 -w14 -h34 -nBackspace)
geos_cvtpcx(mkrNumeric.ui PCX/clcbut02.pcx -2 -f -m13 -t -b -anormal -x0 -y0
            -w19 -h34 -nNumeric)
geos_cvtpcx(mkrAlphabetic.ui PCX/clcbut03.pcx -2 -f -m13 -t -b -anormal -x0
            -y0 -w22 -h34 -nAlphabetic)

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

geos_art(spreadsheet)
