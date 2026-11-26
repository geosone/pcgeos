/*
 * ansic_runtime.c -- after ansic_runtime.asm: TrueType_GrMulWWFixed.
 */
#include "../Adapter/ttadapter.h"
#include "../Adapter/ttinit.h"
#include "../Adapter/ttwidths.h"
#include "../Adapter/ttchars.h"
#include "../Adapter/ttmetrics.h"
#include "../Adapter/ttpath.h"
#include <heap.h>
#include <geode.h>
#include <Internal/window.h>

WWFixedAsDWord _pascal TrueType_GrMulWWFixed(WWFixedAsDWord i,
					     WWFixedAsDWord j)
{
    /* the middle 32 bits of the product (TrueType_GrRegMul32ToDDF) */
    return (WWFixedAsDWord) (sdword) (((int64_t) (sdword) i *
				       (sdword) j) >> 16);
}
