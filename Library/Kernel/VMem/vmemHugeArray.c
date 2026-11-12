/*
 * vmemHugeArray.c -- huge arrays (after vmemHugeArray.asm).
 *
 * A huge array is a VM chain of LMem blocks, as in 16-bit GEOS, and is
 * kept in the same form in the file (Port32/ABI.md 13b):
 *
 *   the directory block (user ID SVMID_HA_DIR_ID)
 *	HugeArrayDirectory: HAD_data (the first data block, also the
 *	chain link), HAD_dir (the chunk of the directory), HAD_xdir (0),
 *	HAD_self, HAD_size (element size, 0: variable), then the user's
 *	part of the header; the directory: a chunk array of
 *	HugeArrayDirEntry, one per data block, after a first entry with
 *	HADE_last -1
 *   the data blocks (SVMID_HA_BLOCK_ID)
 *	HugeArrayBlock: HAB_next (also the chain link), HAB_prev,
 *	HAB_dir; the elements: one chunk array (the first chunk)
 *
 * HADE_last is the number of the last element of the block,
 * HADE_size the size of the block in the file.  The asm swapped the
 * chain link and LMBH_handle when it locked and unlocked a block; here
 * the link is written to the file's LMBH_handle (VMHugeArrayToDOS16)
 * and the chain routines read it from HAD_data/HAB_next.
 *
 * Blocks: an insertion that keeps the block within HA_UPPER_LIMIT goes
 * into it; a larger one splits the block there and adds new blocks of
 * about HA_DESIRED_BLOCK_SIZE (fixed size elements) or of one element
 * each (variable size).  After insertions and deletions neighbouring
 * blocks that fit within HA_UPPER_LIMIT together are combined.  The
 * asm combined only around the place of the change; the blocks may be
 * cut differently, which the format allows.
 *
 * Element pointers are pointers into the locked data block; the
 * routines that take one find the block with MemPtrToHandle.
 */
#include "vmemInt.h"
#include <lmem.h>
#include <chunkarr.h>
#include <hugearr.h>
#include <string.h>

typedef struct {
    LMemBlockHeader	HAB_header;
    VMBlockHandle	HAB_next;
    VMBlockHandle	HAB_prev;
    VMBlockHandle	HAB_dir;
} HugeArrayBlock;

typedef struct {
    dword		HADE_last;
    word		HADE_size;
    VMBlockHandle	HADE_handle;
} HugeArrayDirEntry;

#define HA_DESIRED_BLOCK_SIZE	4000
#define HA_UPPER_LIMIT		((3 * HA_DESIRED_BLOCK_SIZE) / 2)

#define DOS16_LMBH_SIZE		16
#define LMBH_DELTA		((word) (sizeof(LMemBlockHeader) - \
					 DOS16_LMBH_SIZE))

typedef struct {
    VMFileHandle	file;
    VMBlockHandle	self;
    MemHandle		mh;
} HADir;

/* ---------------------------------------------------------------------
 * The directory
 * ------------------------------------------------------------------- */

static HugeArrayDirectory *DirHdr(const HADir *d)
{
    return MemDeref(d->mh);
}

static optr DirArr(const HADir *d)
{
    return ConstructOptr(d->mh, DirHdr(d)->HAD_dir);
}

static word Entries(const HADir *d)		/* with the first one */
{
    return ChunkArrayGetCount(DirArr(d));
}

static HugeArrayDirEntry *Ent(const HADir *d, word i)
{
    return ChunkArrayElementToPtr(DirArr(d), i, NULL);
}

static void EnterDir(HADir *d, VMFileHandle file, VMBlockHandle block)
{
    d->file = file;
    d->self = block;
    VMLock(file, block, &d->mh);
}

static void LeaveDir(HADir *d, Boolean dirty)
{
    if (dirty)
	VMDirty(d->mh);
    VMUnlock(d->mh);
}

static dword Count(const HADir *d)
{
    return Ent(d, (word) (Entries(d) - 1))->HADE_last + 1;
}

static dword FirstOf(const HADir *d, word i)
{
    return Ent(d, (word) (i - 1))->HADE_last + 1;
}

static word BlockCount(const HADir *d, word i)
{
    return (word) (Ent(d, i)->HADE_last - FirstOf(d, i) + 1);
}

/* ScanHADirectory: the entry of the block holding elem, 0 if none */
static word Find(const HADir *d, dword elem)
{
    word n = Entries(d), i;

    for (i = 1; i < n; i++)
	if (Ent(d, i)->HADE_last >= elem)
	    return i;
    return 0;
}

/* ModifyElementCounts: entries i and after */
static void Counts(const HADir *d, word i, sdword delta)
{
    word n = Entries(d);

    for (; i < n; i++)
	Ent(d, i)->HADE_last += (dword) delta;
}

/* ---------------------------------------------------------------------
 * Data blocks
 * ------------------------------------------------------------------- */

static optr DataArr(MemHandle mh)
{
    return ConstructOptr(mh, ((LMemBlockHeader *) MemDeref(mh))->LMBH_offset);
}

static MemHandle LockBlock(const HADir *d, word i)
{
    MemHandle mh;

    VMLock(d->file, Ent(d, i)->HADE_handle, &mh);
    return mh;
}

/* the block changed: its size to the directory, unlocked */
static void DoneBlock(const HADir *d, word i, MemHandle mh)
{
    Ent(d, i)->HADE_size = LMemDOS16Size(mh);
    VMDirty(mh);
    VMUnlock(mh);
}

static void SetNext(const HADir *d, VMBlockHandle b, VMBlockHandle next)
{
    MemHandle mh;

    if (!b) {
	DirHdr(d)->HAD_data = next;
	return;
    }
    ((HugeArrayBlock *) VMLock(d->file, b, &mh))->HAB_next = next;
    VMDirty(mh);
    VMUnlock(mh);
}

static void SetPrev(const HADir *d, VMBlockHandle b, VMBlockHandle prev)
{
    MemHandle mh;

    if (!b)
	return;
    ((HugeArrayBlock *) VMLock(d->file, b, &mh))->HAB_prev = prev;
    VMDirty(mh);
    VMUnlock(mh);
}

/* AllocNewDataBlock: a new empty block at entry i, linked in; locked */
static MemHandle NewBlock(const HADir *d, word i)
{
    word n = Entries(d);
    VMBlockHandle b = VMAllocLMem(d->file, LMEM_TYPE_GENERAL,
				  sizeof(HugeArrayBlock));
    VMBlockHandle prev = Ent(d, (word) (i - 1))->HADE_handle;
    VMBlockHandle next = i < n ? Ent(d, i)->HADE_handle : 0;
    dword last = Ent(d, (word) (i - 1))->HADE_last;
    HugeArrayDirEntry *e;
    HugeArrayBlock *h;
    MemHandle mh;

    VMModifyUserID(d->file, b, SVMID_HA_BLOCK_ID);
    VMLock(d->file, b, &mh);
    ChunkArrayCreateAt(ConstructOptr(mh, 0), DirHdr(d)->HAD_size, 0, 0);
    e = i < n ? ChunkArrayInsertAt(DirArr(d), Ent(d, i),
				   sizeof(HugeArrayDirEntry))
	      : ChunkArrayAppend(DirArr(d), sizeof(HugeArrayDirEntry));
    e->HADE_last = last;
    e->HADE_handle = b;
    h = MemDeref(mh);
    h->HAB_next = next;
    h->HAB_prev = prev;
    h->HAB_dir = d->self;
    SetNext(d, prev, b);
    SetPrev(d, next, b);
    Ent(d, i)->HADE_size = LMemDOS16Size(mh);
    VMDirty(mh);
    return mh;
}

/* FreeHABlock: the block of entry i out of the chain and the directory
 * (the counts of the others stay) */
static void FreeBlock(const HADir *d, word i)
{
    word n = Entries(d);
    VMBlockHandle b = Ent(d, i)->HADE_handle;
    VMBlockHandle prev = Ent(d, (word) (i - 1))->HADE_handle;
    VMBlockHandle next = i + 1u < n ? Ent(d, (word) (i + 1))->HADE_handle
				    : 0;

    SetNext(d, prev, next);
    SetPrev(d, next, prev);
    VMFree(d->file, b);
    ChunkArrayDeleteRange(DirArr(d), i, 1);
}

/* room for k elements at index at of a chunk array (sizes: the size of
 * each, for variable size elements); the first of them */
static byte *InsertElems(optr arr, word at, word k, const word *sizes)
{
    ChunkArrayHeader *h = LMemDeref(arr);
    word es = h->CAH_elementSize, j;

    if (es) {
	if (LMemInsertAt(arr, (word) (h->CAH_offset + at * es),
			 (word) (k * es)))
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
	h = LMemDeref(arr);
	h->CAH_count = (word) (h->CAH_count + k);
	return ChunkArrayElementToPtr(arr, at, NULL);
    }
    for (j = 0; j < k; j++) {
	if ((word) (at + j) >= ChunkArrayGetCount(arr))
	    ChunkArrayAppend(arr, sizes[j]);
	else
	    ChunkArrayInsertAt(arr, ChunkArrayElementToPtr(arr,
							   (word) (at + j),
							   NULL), sizes[j]);
    }
    return ChunkArrayElementToPtr(arr, at, NULL);
}

/* k elements from index from of block src to index at of block dst */
static void MoveElems(MemHandle src, word from, word k, MemHandle dst,
		      word at)
{
    optr sa = DataArr(src), da = DataArr(dst);
    word es = ((ChunkArrayHeader *) LMemDeref(sa))->CAH_elementSize, j;

    if (!k)
	return;
    if (es) {
	byte *q = InsertElems(da, at, k, NULL);
	memcpy(q, ChunkArrayElementToPtr(sa, from, NULL), (size_t) k * es);
    } else {
	for (j = 0; j < k; j++) {
	    word sz;
	    byte *p = ChunkArrayElementToPtr(sa, (word) (from + j), &sz);
	    byte *q = InsertElems(da, (word) (at + j), 1, &sz);
	    p = ChunkArrayElementToPtr(sa, (word) (from + j), &sz);
	    memcpy(q, p, sz);
	}
    }
    ChunkArrayDeleteRange(sa, from, k);
}

/* SplitHABlock: the elements of block i from index at on to a new
 * block after it */
static void Split(const HADir *d, word i, word at)
{
    MemHandle a = LockBlock(d, i), b = NewBlock(d, (word) (i + 1));
    word k = (word) (BlockCount(d, i) - at);

    MoveElems(a, at, k, b, 0);
    Ent(d, i)->HADE_last -= k;
    DoneBlock(d, i, a);
    DoneBlock(d, (word) (i + 1), b);
}

/* CombineHABlocks: block i + 1 into block i */
static void Combine(const HADir *d, word i)
{
    MemHandle a = LockBlock(d, i), b = LockBlock(d, (word) (i + 1));

    MoveElems(b, 0, BlockCount(d, (word) (i + 1)), a, BlockCount(d, i));
    Ent(d, i)->HADE_last = Ent(d, (word) (i + 1))->HADE_last;
    DoneBlock(d, i, a);
    VMUnlock(b);
    FreeBlock(d, (word) (i + 1));
}

/* CollectGarbage: neighbours that fit together combined */
static void Garbage(const HADir *d)
{
    word i = 1;

    while (i + 1u < Entries(d)) {
	if ((dword) Ent(d, i)->HADE_size +
	    Ent(d, (word) (i + 1))->HADE_size <= HA_UPPER_LIMIT)
	    Combine(d, i);
	else
	    i++;
    }
}

/* ---------------------------------------------------------------------
 * Inserting and deleting
 * ------------------------------------------------------------------- */

/* InsertElements, AllocHAChain, InitHAChain: num elements (fixed size)
 * or one element of num bytes before elem (at the end past it) */
static void Insert(const HADir *d, dword elem, word num, const void *data)
{
    word es = DirHdr(d)->HAD_size;
    word k = es ? num : 1, per, i, p, ca;
    dword bytes = es ? (dword) num * es : num, total = Count(d);
    const byte *src = data;
    MemHandle mh;

    if (!num && es)
	return;
    if (elem >= total) {			/* at the end */
	elem = total;
	i = (word) (Entries(d) - 1);
	ca = i ? BlockCount(d, i) : 0;
    } else {
	i = Find(d, elem);
	ca = (word) (elem - FirstOf(d, i));
    }
    if (i && Ent(d, i)->HADE_size + bytes <= HA_UPPER_LIMIT) {
	byte *q;
	mh = LockBlock(d, i);
	q = InsertElems(DataArr(mh), ca, k, &num);
	if (src)
	    memcpy(q, src, bytes);
	Counts(d, i, k);
	DoneBlock(d, i, mh);
	return;
    }
    if (!i)
	p = 1;
    else if (!ca)
	p = i;
    else if (ca >= BlockCount(d, i))
	p = (word) (i + 1);
    else {
	Split(d, i, ca);
	p = (word) (i + 1);
    }
    per = es ? (word) (HA_DESIRED_BLOCK_SIZE / es + 1) : 1;
    while (k) {
	word m = k < per ? k : per;
	byte *q;
	mh = NewBlock(d, p);
	q = InsertElems(DataArr(mh), 0, m, &num);
	if (src) {
	    dword n = es ? (dword) m * es : num;
	    memcpy(q, src, n);
	    src += n;
	}
	Counts(d, p, m);
	DoneBlock(d, p, mh);
	p++;
	k = (word) (k - m);
    }
    Garbage(d);
}

static void Delete(const HADir *d, dword elem, word num)
{
    while (num) {
	word i = Find(d, elem), ca, cnt, m;
	if (!i)
	    break;
	ca = (word) (elem - FirstOf(d, i));
	cnt = BlockCount(d, i);
	m = (word) (cnt - ca) < num ? (word) (cnt - ca) : num;
	if (!ca && m == cnt)
	    FreeBlock(d, i);
	else {
	    MemHandle mh = LockBlock(d, i);
	    ChunkArrayDeleteRange(DataArr(mh), ca, m);
	    DoneBlock(d, i, mh);
	}
	Counts(d, i, -(sdword) m);
	num = (word) (num - m);
    }
    Garbage(d);
}

/* ---------------------------------------------------------------------
 * The API
 * ------------------------------------------------------------------- */

VMBlockHandle HugeArrayCreate(VMFileHandle vmFile, word elemSize,
			      word headerSpace)
{
    VMBlockHandle b = VMAllocLMem(vmFile, LMEM_TYPE_GENERAL,
				  headerSpace ? headerSpace
					      : sizeof(HugeArrayDirectory));
    HugeArrayDirectory *h;
    HugeArrayDirEntry *e;
    MemHandle mh;
    ChunkHandle ch;

    VMModifyUserID(vmFile, b, SVMID_HA_DIR_ID);
    VMLock(vmFile, b, &mh);
    ch = ChunkArrayCreateAt(ConstructOptr(mh, 0), sizeof(HugeArrayDirEntry),
			    0, 0);
    h = MemDeref(mh);
    h->HAD_size = elemSize;
    h->HAD_self = b;
    h->HAD_data = 0;
    h->HAD_xdir = 0;
    h->HAD_dir = ch;
    e = ChunkArrayAppend(ConstructOptr(mh, ch), sizeof(HugeArrayDirEntry));
    e->HADE_last = 0xffffffffUL;
    e->HADE_size = 0;
    e->HADE_handle = 0;
    VMDirty(mh);
    VMUnlock(mh);
    return b;
}

void HugeArrayDestroy(VMFileHandle vmFile, VMBlockHandle vmBlock)
{
    VMFreeVMChain(vmFile, VMCHAIN_MAKE_FROM_VM_BLOCK(vmBlock));
}

dword HugeArrayGetCount(VMFileHandle vmFile, VMBlockHandle vmBlock)
{
    HADir d;
    dword n;

    EnterDir(&d, vmFile, vmBlock);
    n = Count(&d);
    LeaveDir(&d, FALSE);
    return n;
}

/* the element locked: (elements before and with it in the block) << 16
 * | (elements from it on in the block); 0 if there is none */
dword HugeArrayLock(VMFileHandle vmFile, VMBlockHandle vmBlock,
		    dword elemNum, void **elemPtr, word *elemSize)
{
    HADir d;
    word i, ca, cnt, size = 0;
    MemHandle mh;
    void *p;

    EnterDir(&d, vmFile, vmBlock);
    i = Find(&d, elemNum);
    if (!i) {
	if (elemSize)
	    *elemSize = DirHdr(&d)->HAD_size;
	if (elemPtr)
	    *elemPtr = NULL;
	LeaveDir(&d, FALSE);
	return 0;
    }
    ca = (word) (elemNum - FirstOf(&d, i));
    cnt = BlockCount(&d, i);
    mh = LockBlock(&d, i);
    LeaveDir(&d, FALSE);
    p = ChunkArrayElementToPtr(DataArr(mh), ca, &size);
    if (elemPtr)
	*elemPtr = p;
    if (elemSize)
	*elemSize = size;
    return ((dword) (ca + 1) << 16) | (word) (cnt - ca);
}

void HugeArrayUnlock(void *elemPtr)
{
    VMUnlock(MemPtrToHandle(elemPtr));
}

void HugeArrayLockDir(VMFileHandle vmFile, VMBlockHandle vmBlock,
		      void **elemPtr)
{
    MemHandle mh;

    *elemPtr = VMLock(vmFile, vmBlock, &mh);
}

void HugeArrayUnlockDir(void *elemPtr)
{
    VMUnlock(MemPtrToHandle(elemPtr));
}

void HugeArrayDirty(const void *elemPtr)
{
    VMDirty(MemPtrToHandle((void *) elemPtr));
}

dword HugeArrayAppend(VMFileHandle vmFile, VMBlockHandle vmBlock,
		      word numElem, const void *initData)
{
    HADir d;
    dword n;

    EnterDir(&d, vmFile, vmBlock);
    n = Count(&d);
    Insert(&d, n, numElem, initData);
    LeaveDir(&d, TRUE);
    return n;
}

void HugeArrayInsert(VMFileHandle vmFile, VMBlockHandle vmBlock,
		     word numElem, dword elemNum, const void *initData)
{
    HADir d;

    EnterDir(&d, vmFile, vmBlock);
    Insert(&d, elemNum, numElem, initData);
    LeaveDir(&d, TRUE);
}

void HugeArrayDelete(VMFileHandle vmFile, VMBlockHandle vmBlock,
		     word numElem, dword elemNum)
{
    HADir d;

    EnterDir(&d, vmFile, vmBlock);
    Delete(&d, elemNum, numElem);
    LeaveDir(&d, TRUE);
}

/* fixed size: numElem elements; variable: one, of numElem bytes */
void HugeArrayReplace(VMFileHandle vmFile, VMBlockHandle vmBlock,
		      word numElem, dword elemNum, const void *initData)
{
    HADir d;

    EnterDir(&d, vmFile, vmBlock);
    if (DirHdr(&d)->HAD_size) {
	if (numElem) {
	    Delete(&d, elemNum, numElem);
	    Insert(&d, elemNum, numElem, initData);
	}
    } else {
	Delete(&d, elemNum, 1);
	if (numElem)
	    Insert(&d, elemNum, numElem, initData);
    }
    LeaveDir(&d, TRUE);
}

void HugeArrayResize(VMFileHandle vmFile, VMBlockHandle vmBlock,
		     dword elemNum, word newSize)
{
    HADir d;
    word i, ca;
    MemHandle mh;

    EnterDir(&d, vmFile, vmBlock);
    i = Find(&d, elemNum);
    if (i) {
	ca = (word) (elemNum - FirstOf(&d, i));
	mh = LockBlock(&d, i);
	ChunkArrayElementResize(DataArr(mh), ca, newSize);
	DoneBlock(&d, i, mh);
	if (Ent(&d, i)->HADE_size > HA_UPPER_LIMIT) {
	    /* the element in a block of its own */
	    if (ca + 1u < BlockCount(&d, i))
		Split(&d, i, (word) (ca + 1));
	    if (ca)
		Split(&d, i, ca);
	}
	Garbage(&d);
    }
    LeaveDir(&d, TRUE);
}

/* the element pointer's block, file, directory and element number */
static void Where(void *elemPtr, HADir *d, dword *elem)
{
    MemHandle mh = MemPtrToHandle(elemPtr);
    VMFileHandle file;
    VMBlockHandle b = VMMemBlockToVMBlock(mh, &file);
    VMBlockHandle dir = ((HugeArrayBlock *) MemDeref(mh))->HAB_dir;
    word ca = ChunkArrayPtrToElement(DataArr(mh), elemPtr), n, i;

    VMUnlock(mh);
    EnterDir(d, file, dir);
    n = Entries(d);
    for (i = 1; i < n && Ent(d, i)->HADE_handle != b; i++)
	;
    *elem = FirstOf(d, i) + ca;
}

word HugeArrayExpand(void **elemPtr, word numElem, const void *initData)
{
    HADir d;
    dword elem;

    Where(*elemPtr, &d, &elem);
    Insert(&d, elem, numElem, initData);
    LeaveDir(&d, TRUE);
    return (word) HugeArrayLock(d.file, d.self, elem, elemPtr, NULL);
}

word HugeArrayContract(void **elemPtr, word numElem)
{
    HADir d;
    dword elem;

    Where(*elemPtr, &d, &elem);
    Delete(&d, elem, numElem);
    LeaveDir(&d, TRUE);
    return (word) HugeArrayLock(d.file, d.self, elem, elemPtr, NULL);
}

/* the next element (in the next block at the end of one): the elements
 * from it on in its block; 0 at the last element (nothing changes) */
word HugeArrayNext(void **elemPtr, word *size)
{
    MemHandle mh = MemPtrToHandle(*elemPtr);
    optr arr = DataArr(mh);
    word ca = ChunkArrayPtrToElement(arr, *elemPtr);
    word cnt = ChunkArrayGetCount(arr), sz = 0;
    VMBlockHandle next;
    VMFileHandle file;

    if (ca + 1u < cnt) {
	*elemPtr = ChunkArrayElementToPtr(arr, (word) (ca + 1), &sz);
	if (size)
	    *size = sz;
	return (word) (cnt - ca - 1);
    }
    next = ((HugeArrayBlock *) MemDeref(mh))->HAB_next;
    if (!next)
	return 0;
    VMMemBlockToVMBlock(mh, &file);
    VMUnlock(mh);
    VMLock(file, next, &mh);
    arr = DataArr(mh);
    *elemPtr = ChunkArrayElementToPtr(arr, 0, &sz);
    if (size)
	*size = sz;
    return ChunkArrayGetCount(arr);
}

/* the previous element (elemPtr1) and the first of its block
 * (elemPtr2): the elements from the first to it; 0 at the first */
word HugeArrayPrev(void **elemPtr1, void **elemPtr2, word *size)
{
    MemHandle mh = MemPtrToHandle(*elemPtr1);
    optr arr = DataArr(mh);
    word ca = ChunkArrayPtrToElement(arr, *elemPtr1), sz = 0;
    VMBlockHandle prev;
    VMFileHandle file;

    if (!ca) {
	prev = ((HugeArrayBlock *) MemDeref(mh))->HAB_prev;
	if (!prev)
	    return 0;
	VMMemBlockToVMBlock(mh, &file);
	VMUnlock(mh);
	VMLock(file, prev, &mh);
	arr = DataArr(mh);
	ca = ChunkArrayGetCount(arr);
    }
    *elemPtr1 = ChunkArrayElementToPtr(arr, (word) (ca - 1), &sz);
    *elemPtr2 = ChunkArrayElementToPtr(arr, 0, NULL);
    if (size)
	*size = sz;
    return ca;
}

Boolean HugeArrayEnum(VMFileHandle vmFile, VMBlockHandle vmBlock,
		      Boolean (*callback)(void *element, void *enumData),
		      dword startElement, dword count, void *enumData)
{
    HADir d;
    dword elem = startElement;

    EnterDir(&d, vmFile, vmBlock);
    while (count) {
	word i = Find(&d, elem), ca, cnt, j;
	MemHandle mh;
	if (!i)
	    break;
	ca = (word) (elem - FirstOf(&d, i));
	cnt = BlockCount(&d, i);
	mh = LockBlock(&d, i);
	for (j = ca; j < cnt && count; j++, count--, elem++) {
	    if (callback(ChunkArrayElementToPtr(DataArr(mh), j, NULL),
			 enumData)) {
		VMDirty(mh);
		VMUnlock(mh);
		LeaveDir(&d, FALSE);
		return TRUE;
	    }
	}
	VMDirty(mh);
	VMUnlock(mh);
    }
    LeaveDir(&d, FALSE);
    return FALSE;
}

void HugeArrayCompressBlocks(VMFileHandle vmFile, VMBlockHandle vmBlock)
{
    HADir d;
    word n, i;

    EnterDir(&d, vmFile, vmBlock);
    n = Entries(&d);
    for (i = 1; i < n; i++) {
	MemHandle mh = LockBlock(&d, i);
	LMemContract(mh);
	DoneBlock(&d, i, mh);
    }
    LeaveDir(&d, TRUE);
}

void ECCheckHugeArray(VMFileHandle vmFile, VMBlockHandle vmBlock)
{
    (void) vmFile;
    (void) vmBlock;
}

/* ---------------------------------------------------------------------
 * For the VM code (vmemInt.h)
 * ------------------------------------------------------------------- */

/* the chain link of a block in memory (p) with user ID uid */
word VMHugeArrayLink(word uid, const void *p, Boolean *isHA)
{
    *isHA = TRUE;
    if (uid == SVMID_HA_DIR_ID)
	return ((const HugeArrayDirectory *) p)->HAD_data;
    if (uid == SVMID_HA_BLOCK_ID)
	return ((const HugeArrayBlock *) p)->HAB_next;
    *isHA = FALSE;
    return 0;
}

Boolean VMHugeArraySetLink(word uid, void *p, word link)
{
    if (uid == SVMID_HA_DIR_ID)
	((HugeArrayDirectory *) p)->HAD_data = link;
    else if (uid == SVMID_HA_BLOCK_ID)
	((HugeArrayBlock *) p)->HAB_next = link;
    else
	return FALSE;
    return TRUE;
}

static word Get16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static void Put16(byte *p, word v)
{
    p[0] = (byte) v;
    p[1] = (byte) (v >> 8);
}

/* the DOS16 image of an LMem block (LMemToDOS16): the chain link in
 * LMBH_handle, as UnlockHABlock left it; HAD_dir as a file chunk
 * handle */
void VMHugeArrayToDOS16(word uid, byte *img)
{
    if (uid == SVMID_HA_DIR_ID) {
	Put16(img, Get16(img + DOS16_LMBH_SIZE));
	Put16(img + DOS16_LMBH_SIZE + 2,
	      (word) (Get16(img + DOS16_LMBH_SIZE + 2) - LMBH_DELTA));
    } else if (uid == SVMID_HA_BLOCK_ID)
	Put16(img, Get16(img + DOS16_LMBH_SIZE));
}

void VMHugeArrayFromDOS16(word uid, MemHandle mh)
{
    if (uid == SVMID_HA_DIR_ID) {
	HugeArrayDirectory *h = MemLock(mh);
	h->HAD_dir = (ChunkHandle) (h->HAD_dir + LMBH_DELTA);
	MemUnlock(mh);
    }
}

/* FixupHugeArrayChain: a copied array's own handles (the chain links
 * are those of the copy) */
void VMHugeArrayFixup(VMFileHandle file, VMBlockHandle dir)
{
    HADir d;
    word n, i;
    VMBlockHandle b, prev = 0;

    EnterDir(&d, file, dir);
    DirHdr(&d)->HAD_self = dir;
    b = DirHdr(&d)->HAD_data;
    n = Entries(&d);
    for (i = 1; i < n && b; i++) {
	MemHandle mh;
	HugeArrayBlock *h = VMLock(file, b, &mh);
	Ent(&d, i)->HADE_handle = b;
	h->HAB_prev = prev;
	h->HAB_dir = dir;
	prev = b;
	b = h->HAB_next;
	VMDirty(mh);
	VMUnlock(mh);
    }
    LeaveDir(&d, TRUE);
}

/* VMCompareVMChains of two arrays: the same element size, user header
 * and elements */
Boolean VMHugeArrayCompare(VMFileHandle fa, VMBlockHandle a,
			   VMFileHandle fb, VMBlockHandle b)
{
    HADir da, db;
    Boolean same;
    dword n, i;

    EnterDir(&da, fa, a);
    EnterDir(&db, fb, b);
    {
	const byte *ha = MemDeref(da.mh), *hb = MemDeref(db.mh);
	word ua = ((const LMemBlockHeader *) ha)->LMBH_offset;
	word ub = ((const LMemBlockHeader *) hb)->LMBH_offset;
	n = Count(&da);
	same = DirHdr(&da)->HAD_size == DirHdr(&db)->HAD_size &&
	       n == Count(&db) && ua == ub &&
	       memcmp(ha + sizeof(HugeArrayDirectory),
		      hb + sizeof(HugeArrayDirectory),
		      ua - sizeof(HugeArrayDirectory)) == 0;
    }
    LeaveDir(&da, FALSE);
    LeaveDir(&db, FALSE);
    for (i = 0; i < n && same; i++) {
	void *pa, *pb;
	word sa, sb;
	HugeArrayLock(fa, a, i, &pa, &sa);
	HugeArrayLock(fb, b, i, &pb, &sb);
	same = sa == sb && memcmp(pa, pb, sa) == 0;
	HugeArrayUnlock(pa);
	HugeArrayUnlock(pb);
    }
    return same;
}
