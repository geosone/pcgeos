/*
 * dbaseInt.h -- the database manager of the C kernel (after
 * Include/Internal/dbaseInt.def and dbaseConstant.def).
 *
 * A database lives in a VM file.  A group is a VM block (DB_GROUP_ID)
 * with a DBGroupHeader and a heap of fixed-size structures: one
 * DBItemInfo per item, one DBItemBlockInfo per item block.  An item is
 * the offset of its DBItemInfo in the group block; its data is a chunk
 * in an item block, an LMem VM block (DB_ITEM_BLOCK_ID) of that group.
 * The DB map block (VMH_dbMapBlock, DB_MAP_ID) holds the map item and
 * the current "ungrouped" group.
 *
 * Port32: the group and map blocks are kept in memory as they are in the
 * file (16-bit fields, little endian like the hosts), so their offsets
 * stay those of DOS16 files.  The memory handle fields (DBGH_handle,
 * DBMB_handle) cannot hold ours and stay 0; the code keeps the handle
 * beside the block.  DBII_chunk is the chunk handle of the file, which is
 * LMEM_DOS16_DELTA lower than the one in memory.
 */
#ifndef DBASEINT_H
#define DBASEINT_H

#include "kernelC.h"
#include <dbase.h>

typedef struct {
    word	DBGH_vmemHandle;	/* the group's own VM block */
    word	DBGH_handle;		/* 0 (DOS16: its memory handle) */
    word	DBGH_flags;		/* GroupFlags */
    word	DBGH_itemBlocks;	/* list of DBItemBlockInfo */
    word	DBGH_itemFreeList;	/* free DBItemInfo */
    word	DBGH_blockFreeList;	/* free DBItemBlockInfo */
    word	DBGH_blockSize;		/* bytes of the group block used */
    word	DBGH_numItems;		/* if GF_NEW_UNGROUP */
} DBGroupHeader;
/* groups of older files end before DBGH_numItems: their structures start
 * at 14 */
#define DB_OLD_GROUP_HEADER_SIZE	14

/* GroupFlags */
#define GF_IS_UNGROUP		0x8000	/* the old "ungroup" group */
#define GF_NEW_UNGROUP		0x4000	/* one of the new "ungroup" set */
#define GF_UNGROUP_FULL		0x2000	/* that one is full */

typedef struct {
    word	DBIBI_next;
    word	DBIBI_block;		/* VM block of the item block */
    sword	DBIBI_refCount;		/* items in it */
} DBItemBlockInfo;

typedef struct {
    word	DBII_block;		/* its DBItemBlockInfo */
    word	DBII_chunk;		/* chunk handle of the file */
} DBItemInfo;

typedef struct {
    LMemBlockHeader	DBIBH_standard;
    word		DBIBH_vmHandle;
    word		DBIBH_infoStruct;	/* its DBItemBlockInfo */
} DBItemBlockHeader;

typedef struct {
    word	DBMB_vmemHandle;
    word	DBMB_handle;		/* 0 (DOS16: its memory handle) */
    word	DBMB_mapGroup;
    word	DBMB_mapItem;
    word	DBMB_ungrouped;		/* the old ungrouped group */
    word	DBMB_newUngrouped;	/* the current new one */
} DBMapBlock;

/* the free lists link through the first word; the structures of a list
 * have the size of the ones they stand for */
#define DB_FREE_ITEM_SIZE	sizeof(DBItemInfo)		/* 4 */
#define DB_FREE_BLOCK_SIZE	sizeof(DBItemBlockInfo)	/* 6 */

#define DB_ITEM_STRUC_INC		64
#define DB_BLOCK_STRUC_INC		5
#define DB_ITEM_BLOCK_INIT_SIZE		256
#define DB_ITEM_BLOCK_DESIRED_SIZE	8192
#define DB_GROUP_BLOCK_DESIRED_SIZE	8192
#define DB_UNGROUP_LOW_WATER_MARK	1536
/* the sizes in DOS16 files, for DBCheckDBFile */
#define DB_DOS16_MAP_BLOCK_SIZE		16	/* 12 rounded to 16 */
#define DB_DOS16_ITEM_BLOCK_HEADER	20

#define DB_CHUNK_EMPTY	0xffffu		/* a chunk without data (LMem) */
#define DB_CHUNK_TO_MEM(c)	((ChunkHandle) ((c) + LMEM_DOS16_DELTA))
#define DB_CHUNK_TO_FILE(c)	((word) ((c) - LMEM_DOS16_DELTA))

/* a locked group block */
typedef struct {
    VMFileHandle	file;
    VMBlockHandle	group;
    MemHandle		mh;
    byte		*p;		/* again after it grows */
} DBGroup_;

#define DBGH(g)		((DBGroupHeader *) (g)->p)
#define DBII(g, off)	((DBItemInfo *) ((g)->p + (off)))
#define DBIBI(g, off)	((DBItemBlockInfo *) ((g)->p + (off)))

/* the lock of the database manager: it nests */
void DBEnter(void);
void DBLeave(void);

/* dbaseGroup.c, dbaseMapBlk */
void DBGroupLock(VMFileHandle file, VMBlockHandle group, DBGroup_ *g);
void DBGroupUnlock(DBGroup_ *g);
void DBGroupDirty(DBGroup_ *g);
VMBlockHandle DBGroupGetUngrouped(VMFileHandle file);
DBMapBlock *DBLockDBMap(VMFileHandle file, MemHandle *mh);

/* dbaseStructure.c */
word DBItemAlloc(DBGroup_ *g);
void DBItemFree(DBGroup_ *g, word item);
word DBBlockAlloc(DBGroup_ *g);
void DBBlockFree(DBGroup_ *g, word info);

/* dbaseItemBlock.c */
word DBItemBlockFind(DBGroup_ *g, word size);
void DBItemBlockFree(DBGroup_ *g, word info);
byte *DBItemBlockLock(DBGroup_ *g, word info, MemHandle *mh);
void DBItemBlockAddRef(DBGroup_ *g, MemHandle mh);
Boolean DBItemBlockDelRef(DBGroup_ *g, MemHandle mh);	/* TRUE: freed */

#endif
