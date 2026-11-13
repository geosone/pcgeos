/*
 * pcl4Tables.c -- the escapes of the LaserJet (PCL 4) driver (the C port
 * of pcl4Tables.asm): the number of copies.
 */
#include "printcom.h"

extern PrintRoutine PrintEscSetCopies;

const PrintEscEntry printEscTable[] = {
    { DR_PRINT_ESC_SET_COPIES, PrintEscSetCopies }
};
const word printNumEscapes = sizeof(printEscTable) / sizeof(printEscTable[0]);
