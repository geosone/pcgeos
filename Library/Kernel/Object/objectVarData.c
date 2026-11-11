/*
 * objectVarData.c -- variable data of objects (after objectVarData.asm
 * and the vardata methods of objectMeta.asm).
 *
 * Variable data follows the instance data: after the last master part
 * (or, while that is not built, after the base structure; for a class
 * without master parts after its instance), at an offset aligned to 8,
 * up to the end of the chunk.  Each entry is a VarDataEntry (4 reserved
 * bytes, type with VarDataFlags, size of entry and data) and its data,
 * and the next entry starts at the next offset aligned to 8, so the data
 * of every entry is 8-aligned.  Port32/ABI.md section 13.
 *
 * Pointers to variable data point to the data (or, for an entry without
 * data, to where it would be), as in the asm kernel; they are good until
 * the next LMem operation on the block.  The C API passes the offset of
 * such a pointer from the start of the block (ObjVarDeleteDataAt,
 * MSG_META_INITIALIZE_VAR_DATA), as the near pointers of 16-bit GEOS.
 */
#include "kernelC.h"

#include <string.h>

#define ENTRY		((word) sizeof(VarDataEntry))
#define TYPE_AT(p, e)	(((VarDataEntry *) ((p) + (e)))->VDE_dataType)
#define SIZE_AT(p, e)	(((VarDataEntry *) ((p) + (e)))->VDE_entrySize)

/* the unclamped start of the variable data, from the chunk start */
static word VarStart(optr o)
{
    byte *p = LMemDeref(o);
    ClassStruct *cls = *(ClassStruct **) p;
    word mo = cls->Class_masterOffset, start;

    if (!mo) {
	start = cls->Class_instanceSize;
    } else {
	word part = *(word *) (p + mo);
	start = part ? (word) (part + cls->Class_instanceSize)
		     : (word) (mo + sizeof(word));
    }
    return (word) OBJ_ALIGN(start);
}

void ObjVarDataArea(optr o, word *start, word *end)
{
    *end = LMemGetChunkSize(o);
    *start = VarStart(o);
    if (*start > *end)
	*start = *end;
}

/* the entry after the one at `e' (offsets from the chunk start) */
static word NextEntry(const byte *p, word e, word end)
{
    word size = SIZE_AT(p, e);

    if (size < ENTRY || (dword) e + size > end)
	KernelFatal(OVS_SIZE_OF_VAR_DATA_ENTRIES_GREATER_THAN_VAR_DATA_AREA);
    e = (word) OBJ_ALIGN((dword) e + size);
    return e > end ? end : e;
}

/* the offset of the entry of `type' in the chunk, or 0 */
static word FindEntry(optr o, word type)
{
    byte *p = LMemDeref(o);
    word e, end;

    ObjVarDataArea(o, &e, &end);
    for (; e < end; e = NextEntry(p, e, end))
	if ((TYPE_AT(p, e) & VDF_TYPE) == (type & VDF_TYPE))
	    return e;
    return 0;
}

void *ObjVarFindData(optr obj, VardataKey dataType)
{
    word e = FindEntry(obj, dataType);

    return e ? (byte *) LMemDeref(obj) + e + ENTRY : NULL;
}

/* objects marked OCF_IGNORE_DIRTY while vardata that is not saved to
 * state changes (PatchObjFlagsForVarData) */
static void ChangedVarData(optr obj, word type)
{
    if (type & VDF_SAVE_TO_STATE)
	ObjMarkDirty(obj);
}

void *ObjVarAddData(optr obj, VardataKey dataType, word dataSize)
{
    word type = (word) (dataType & ~VDF_EXTRA_DATA);
    word size = (word) (ENTRY + dataSize), e, chunkSize, start;
    byte *p;

    if (dataSize)
	type |= VDF_EXTRA_DATA;
    e = FindEntry(obj, type);
    if (e) {
	p = LMemDeref(obj);
	if (SIZE_AT(p, e) != size) {
	    ObjVarDeleteDataAt(obj, (word) (p + e + ENTRY -
					    (byte *) MemDeref(
						OptrToHandle(obj))));
	    e = 0;
	}
    }
    if (!e) {
	/* a new entry at the end, after padding to 8 */
	chunkSize = LMemGetChunkSize(obj);
	start = VarStart(obj);
	e = (word) OBJ_ALIGN(chunkSize > start ? chunkSize : start);
	if (LMemInsertAt(obj, chunkSize, (word) (OBJ_ALIGN(e + size) -
						 chunkSize)))
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
    }
    p = LMemDeref(obj);
    memset(p + e, 0, ENTRY + dataSize);
    TYPE_AT(p, e) = type;
    SIZE_AT(p, e) = size;
    ChangedVarData(obj, type);
    return p + e + ENTRY;
}

void ObjVarDeleteDataAt(optr obj, word extraDataOffset)
{
    byte *b = MemDeref(OptrToHandle(obj));
    byte *p = LMemDeref(obj);
    word e = (word) (b + extraDataOffset - ENTRY - p), next, end, type;

    end = LMemGetChunkSize(obj);
    type = TYPE_AT(p, e);
    next = NextEntry(p, e, end);
    LMemDeleteAt(obj, e, (word) (next - e));
    ChangedVarData(obj, type);
}

/* TRUE if it was there and is deleted (the C stub of the asm kernel) */
Boolean ObjVarDeleteData(optr obj, VardataKey dataType)
{
    word e = FindEntry(obj, dataType);
    byte *p;

    if (!e)
	return FALSE;
    p = LMemDeref(obj);
    ObjVarDeleteDataAt(obj, (word) (p + e + ENTRY -
				    (byte *) MemDeref(OptrToHandle(obj))));
    return TRUE;
}

/* Find the data, or have the object make it with
 * MSG_META_INITIALIZE_VAR_DATA. */
void *ObjVarDerefData(optr obj, VardataKey dataType)
{
    void *d = ObjVarFindData(obj, dataType);
    struct { word dataType; } params;
    word reply = 0;

    if (d)
	return d;
    params.dataType = dataType;
    KernelDeliver(obj, KMSG_META_INITIALIZE_VAR_DATA, &params, &reply);
    d = ObjVarFindData(obj, dataType);
    if (!d || (byte *) d - (byte *) MemDeref(OptrToHandle(obj)) != reply)
	KernelFatal(OVS_BAD_MSG_META_INITIALIZE_VAR_DATA_HANDLER);
    return d;
}

/*
 * Call the handler of each entry's type.  Handlers may change the block
 * (allocate chunks) but not add or remove variable data of this object,
 * as in the asm kernel.
 */
void ObjVarScanData(optr obj, word numHandlers, VarDataCHandler *handlerTable,
		    void *handlerData)
{
    word e, end, i;

    ObjVarDataArea(obj, &e, &end);
    while (e < end) {
	byte *p = LMemDeref(obj);
	word type = (word) (TYPE_AT(p, e) & VDF_TYPE);
	for (i = 0; i < numHandlers; i++)
	    if ((handlerTable[i].VDCH_dataType & VDF_TYPE) == type) {
		handlerTable[i].VDCH_handler(OptrToHandle(obj),
					     OptrToChunk(obj),
					     (VarDataEntry *) (p + e + ENTRY),
					     type, handlerData);
		break;
	    }
	p = LMemDeref(obj);
	end = LMemGetChunkSize(obj);
	e = NextEntry(p, e, end);
    }
}

/* Delete the entries with types in [rangeStart, rangeEnd]; with
 * useStateFlag only those that are not saved to state. */
void ObjVarDeleteDataRange(optr obj, word rangeStart, word rangeEnd,
			   Boolean useStateFlag)
{
    word e, end;

    rangeStart &= VDF_TYPE;
    rangeEnd &= VDF_TYPE;
    ObjVarDataArea(obj, &e, &end);
    while (e < end) {
	byte *p = LMemDeref(obj);
	word type = TYPE_AT(p, e);
	if ((type & VDF_TYPE) >= rangeStart && (type & VDF_TYPE) <= rangeEnd &&
	    !(useStateFlag && (type & VDF_SAVE_TO_STATE))) {
	    ObjVarDeleteDataAt(obj, (word) (p + e + ENTRY -
					    (byte *) MemDeref(
						OptrToHandle(obj))));
	    end = LMemGetChunkSize(obj);
	    continue;			/* the next one moved here */
	}
	e = NextEntry(p, e, end);
    }
}

/* Copy the entries with types in [rangeStart, rangeEnd] to `dest'. */
void ObjVarCopyDataRange(optr source, optr dest, word rangeStart,
			 word rangeEnd)
{
    word e, end;

    rangeStart &= VDF_TYPE;
    rangeEnd &= VDF_TYPE;
    ObjVarDataArea(source, &e, &end);
    while (e < end) {
	byte *p = LMemDeref(source);
	word type = TYPE_AT(p, e);
	if ((type & VDF_TYPE) >= rangeStart && (type & VDF_TYPE) <= rangeEnd) {
	    word n = (type & VDF_EXTRA_DATA)
		     ? (word) (SIZE_AT(p, e) - ENTRY) : 0;
	    byte *d = ObjVarAddData(dest, type, n);
	    p = LMemDeref(source);	/* same block: it may have moved */
	    memcpy(d, p + e + ENTRY, n);
	    end = LMemGetChunkSize(source);
	}
	e = NextEntry(p, e, end);
    }
}
