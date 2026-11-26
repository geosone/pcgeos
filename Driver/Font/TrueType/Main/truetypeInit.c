/*
 * truetypeInit.c -- after truetypeInit.asm: Initialization & exit
 * routines for TrueType font driver
 */
#include "truetypeConstant.h"
#include "../Adapter/ttinit.h"

/* ttinit.c */
TT_Error _pascal Init_FreeType(void);
void _pascal Exit_FreeType(MemHandle varBlock);

Boolean TrueTypeInit(void)
{
    GeodeHandle gh = GeodeGetCodeProcessHandle();

    bitmapHandle = MemAllocSetOwner(gh, TRUETYPE_BLOCK_SIZE,
				    HF_DISCARDABLE | HF_SWAPABLE |
				    HF_SHARABLE | HF_DISCARDED, HAF_NO_ERR);
    variableHandle = MemAllocSetOwner(gh, TRUETYPE_BLOCK_SIZE,
				      HF_SWAPABLE | HF_SHARABLE |
				      HF_DISCARDED, HAF_NO_ERR);
    return (Boolean) (Init_FreeType() != TT_Err_Ok);
}

Boolean TrueTypeExit(void)
{
    Exit_FreeType(variableHandle);
    MemFree(bitmapHandle);
    MemFree(variableHandle);
    bitmapHandle = variableHandle = 0;
    return FALSE;
}

/* TrueTypeInitFonts: the TTF fonts of the font directory into the font
 * info block (DR_bx) */
Boolean TrueTypeInitFonts(DriverRegs *r)
{
    TrueType_InitFonts((MemHandle) r->DR_bx, variableHandle);
    return FALSE;
}
