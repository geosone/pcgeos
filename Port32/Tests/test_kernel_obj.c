/*
 * test_kernel_obj.c -- the object system of the C kernel
 * (Library/Kernel/Object, Port32/ABI.md section 13) with the classes and
 * objects of Goc/kobj.goc: layout, relocation and unrelocation, variable
 * data, ObjInstantiate, MetaClass and ProcessClass, freeing objects and
 * object blocks, composites, GCN lists, detach and quit.
 */
#include "kernelC.h"
#include "ghal.h"
#include <vm.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define GEODE	0x2340

Handle __goc_resource_handles[4];	/* the geode loader fills it */

extern ClassStruct KObjProcessClass, KOMidClass, KOPlainClass;
extern const GocResource __goc_resource_Objects, __goc_resource_Other;
extern const ChunkHandle KOObj1, KOObj2, KOPlain1, KOOther;
extern const ChunkHandle KOTree, KOLeafA, KOLeafB, KOLeafC;
extern const Message kobjGocMessages[];
extern const int kobjGocMessageCount;
extern const word kobjGocVarData[], kobjGocGCNSizes[];
extern int kobjRelocs, kobjUnrelocs;

int KObjLayout(optr obj1, optr obj2);
int KObjRelocated(optr obj1, optr obj2, optr plain, optr other,
		  const Handle *handles);
int KObjHptr(optr obj1, MemHandle other);
int KObjVarData(optr obj1, optr obj2, optr plain);
int KObjInstantiate(MemHandle block);
int KObjClassed(optr mid, optr plain);
void KObjSendFree(optr o);
ClassStruct *KObjClassOf(optr o);
int KObjCompLayout(optr tree, optr a, optr c);
int KObjComposite(optr tree, optr a, optr b, optr c);
int KObjGCNSystem(optr a, optr b, optr c);
int KObjGCNObject(optr tree, optr a, optr b);
int KObjDetach(ThreadHandle t2, ThreadHandle t3);

/* in the order of kobjGocMessages */
static const Message kernelMessages[] = {
    KMSG_META_NULL, KMSG_META_INITIALIZE, KMSG_META_DUMMY, KMSG_META_ACK,
    KMSG_META_GET_CLASS, KMSG_META_IS_OBJECT_IN_CLASS, KMSG_META_BLOCK_FREE,
    KMSG_META_OBJ_FREE, KMSG_META_FINAL_OBJ_FREE,
    KMSG_META_DEC_BLOCK_REF_COUNT, KMSG_META_RESOLVE_VARIANT_SUPERCLASS,
    KMSG_META_RELOCATE, KMSG_META_UNRELOCATE, KMSG_META_SET_FLAGS,
    KMSG_META_GET_FLAGS, KMSG_META_DISPATCH_EVENT,
    KMSG_META_SEND_CLASSED_EVENT, KMSG_META_GET_OPTR,
    KMSG_META_ADD_VAR_DATA, KMSG_META_DELETE_VAR_DATA,
    KMSG_META_INITIALIZE_VAR_DATA, KMSG_META_GET_VAR_DATA,
    KMSG_META_NOTIFY_OBJ_BLOCK_INTERACTIBLE,
    KMSG_META_NOTIFY_OBJ_BLOCK_NOT_INTERACTIBLE,
    KMSG_META_SET_OBJ_BLOCK_OUTPUT, KMSG_META_GET_OBJ_BLOCK_OUTPUT,
    KMSG_PROCESS_INSTANTIATE, KMSG_PROCESS_FINAL_BLOCK_FREE,
    KMSG_PROCESS_INSTANTIATE_FOR_THREAD,
    KMSG_META_NOTIFY, KMSG_META_NOTIFY_WITH_DATA_BLOCK,
    KMSG_META_GCN_LIST_ADD, KMSG_META_GCN_LIST_REMOVE,
    KMSG_META_GCN_LIST_SEND, KMSG_META_GCN_LIST_DESTROY,
    KMSG_META_GCN_LIST_FIND_ITEM,
    KMSG_META_DETACH, KMSG_META_DETACH_COMPLETE, KMSG_META_APP_SHUTDOWN,
    KMSG_META_SHUTDOWN_COMPLETE, KMSG_META_SHUTDOWN_ACK, KMSG_META_QUIT,
    KMSG_META_QUIT_ACK, KMSG_META_FINISH_QUIT, KMSG_PROCESS_EXIT
};
static const word kernelVarData[] = {
    KVD_TEMP_META_GCN, KVD_TEMP_META_QUIT_LIST, KVD_DETACH_DATA
};

/* in the order of kobjGocGCNSizes */
static const word kernelGCNSizes[] = {
    sizeof(GCNListHeader), sizeof(GCNListElement),
    sizeof(GCNListOfListsElement), sizeof(TempMetaGCNData),
    offsetof(GCNListHeader, GCNLH_statusCount),
    sizeof(DetachDataEntry), offsetof(DetachDataEntry, DDE_completeMsg)
};

static int fails;

static void Check(int ok, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL %s\n", what);
    }
}

static word ProcessThread(word unused)
{
    (void) unused;
    ThreadAttachToQueue(GeodeAllocQueue(), &KObjProcessClass);
    return 0;				/* not reached */
}

static int IsHandle(Handle h, HandleEntry *he, void *ctx)
{
    (void) he;
    return h == *(Handle *) ctx;
}

/* is the memory handle still in use? */
static int InUse(MemHandle mh)
{
    return HandleEnum(IsHandle, &mh) != 0;
}

/* a copy of a chunk, to compare after unrelocating */
typedef struct {
    byte	data[256];
    word	size;
} Saved;

static void Save(Saved *s, optr o)
{
    s->size = LMemGetChunkSize(o);
    memcpy(s->data, LMemDeref(o), s->size);
}

static int Same(const Saved *s, optr o)
{
    return s->size == LMemGetChunkSize(o) &&
	   memcmp(s->data, LMemDeref(o), s->size) == 0;
}

int main(void)
{
    ThreadHandle me, t2, t3;
    MemHandle objects, other, blk;
    optr obj1, obj2, plain, oth, o, o2, o3, tree, la, lb, lc;
    Saved s1, s2, sp, so;
    int i;

    kernelCurrentGeode = GEODE;
    me = KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();

    /* the kernel's message numbers are goc's */
    Check(kobjGocMessageCount ==
	  (int) (sizeof(kernelMessages) / sizeof(kernelMessages[0])),
	  "message count");
    for (i = 0; i < kobjGocMessageCount; i++)
	if (kobjGocMessages[i] != kernelMessages[i]) {
	    printf("message %d: goc %u kernel %u\n", i,
		   (unsigned) kobjGocMessages[i],
		   (unsigned) kernelMessages[i]);
	    Check(0, "message number");
	}

    for (i = 0; i < (int) (sizeof(kernelVarData) / sizeof(kernelVarData[0]));
	 i++)
	if (kobjGocVarData[i] != kernelVarData[i]) {
	    printf("vardata %d: goc %u kernel %u\n", i,
		   (unsigned) kobjGocVarData[i], (unsigned) kernelVarData[i]);
	    Check(0, "vardata tag");
	}
    for (i = 0; i < (int) (sizeof(kernelGCNSizes) / sizeof(kernelGCNSizes[0]));
	 i++)
	if (kobjGocGCNSizes[i] != kernelGCNSizes[i]) {
	    printf("GCN structure %d: goc %u kernel %u\n", i,
		   (unsigned) kobjGocGCNSizes[i], (unsigned) kernelGCNSizes[i]);
	    Check(0, "GCN structures");
	}

    /* the resources, as the geode loader will build them */
    objects = LMemCreateResourceBlock(&__goc_resource_Objects, GEODE,
				      HF_SWAPABLE, 0);
    other = LMemCreateResourceBlock(&__goc_resource_Other, GEODE,
				    HF_SWAPABLE, 0);
    MemModifyOtherInfo(objects, me);
    MemModifyOtherInfo(other, me);
    __goc_resource_handles[0] = me;	/* the process */
    __goc_resource_handles[2] = objects;
    __goc_resource_handles[3] = other;
    GeodeSetResourceHandles(GEODE, __goc_resource_handles, 4);
    obj1 = ConstructOptr(objects, KOObj1);
    obj2 = ConstructOptr(objects, KOObj2);
    plain = ConstructOptr(objects, KOPlain1);
    oth = ConstructOptr(other, KOOther);
    tree = ConstructOptr(objects, KOTree);
    la = ConstructOptr(objects, KOLeafA);
    lb = ConstructOptr(other, KOLeafB);
    lc = ConstructOptr(objects, KOLeafC);

    fails += KObjLayout(obj1, obj2);
    fails += KObjCompLayout(tree, la, lc);
    Save(&s1, obj1);
    Save(&s2, obj2);
    Save(&sp, plain);
    Save(&so, oth);

    /* relocate, unrelocate: what goc made, relocate again */
    ObjRelocateObjBlock(objects, VMRT_RELOCATE_FROM_RESOURCE);
    ObjRelocateObjBlock(other, VMRT_RELOCATE_FROM_RESOURCE);
    Check(kobjRelocs == 1, "relocation method");
    Check((((LMemBlockHeader *) MemDeref(objects))->LMBH_flags &
	   LMF_RELOCATED) != 0, "LMF_RELOCATED");
    fails += KObjRelocated(obj1, obj2, plain, oth, __goc_resource_handles);
    ObjUnRelocateObjBlock(objects, VMRT_UNRELOCATE_FROM_RESOURCE);
    ObjUnRelocateObjBlock(other, VMRT_UNRELOCATE_FROM_RESOURCE);
    Check(kobjUnrelocs == 1, "unrelocation method");
    Check(Same(&s1, obj1) && Same(&s2, obj2) && Same(&sp, plain) &&
	  Same(&so, oth), "unrelocated as goc made it");
    ObjRelocateObjBlock(objects, VMRT_RELOCATE_FROM_RESOURCE);
    ObjRelocateObjBlock(other, VMRT_RELOCATE_FROM_RESOURCE);
    fails += KObjRelocated(obj1, obj2, plain, oth, __goc_resource_handles);
    fails += KObjHptr(obj1, other);
    LMemValidate(objects);

    fails += KObjVarData(obj1, obj2, plain);
    LMemValidate(objects);
    fails += KObjInstantiate(objects);
    LMemValidate(objects);
    fails += KObjClassed(obj1, plain);
    fails += KObjComposite(tree, la, lb, lc);
    fails += KObjGCNSystem(la, lb, lc);
    fails += KObjGCNObject(tree, la, lb);
    LMemValidate(objects);
    LMemValidate(objects);
    LMemValidate(other);

    /* an object of a resource keeps its handle when freed */
    ObjFreeChunk(plain);
    Check(LMemGetChunkSize(plain) == 0 && (ObjGetFlags(plain) & OCF_DIRTY),
	  "resource object freed");
    LMemValidate(objects);

    /* a process thread runs a block: remote instantiation, freeing */
    t2 = ThreadCreate(PRIORITY_STANDARD, 0, ProcessThread, 0, GEODE);
    while (!ThreadGetInfo(t2, TGIT_QUEUE_HANDLE))
	TimerSleep(1);			/* let it attach (slow under QEMU) */
    blk = MemAllocLMem(LMEM_TYPE_OBJ_BLOCK, 0);
    MemModifyOtherInfo(blk, t2);
    o = ObjInstantiate(blk, &KOPlainClass);	/* MSG_PROCESS_INSTANTIATE */
    Check(OptrToHandle(o) == blk && KObjClassOf(o) == &KOPlainClass,
	  "remote ObjInstantiate");
    o2 = ObjInstantiateForThread(t2, &KOMidClass);
    o3 = ObjInstantiateForThread(t2, &KOPlainClass);
    Check(OptrToHandle(o2) == OptrToHandle(o3) &&
	  MemGetInfo(OptrToHandle(o2), MGIT_EXEC_THREAD) == t2 &&
	  KObjClassOf(o2) == &KOMidClass, "ObjInstantiateForThread");

    KObjSendFree(o);			/* through t2's queue */
    (void) KObjClassOf(o3);		/* after MSG_META_OBJ_FREE, which
					 * queues MSG_META_FINAL_OBJ_FREE */
    (void) KObjClassOf(o3);		/* after that */
    Check(*(word *) ((byte *) MemDeref(blk) + OptrToChunk(o)) == 0,
	  "MSG_META_OBJ_FREE");

    ObjIncInUseCount(blk);
    ObjFreeObjBlock(blk);		/* in use: later */
    (void) KObjClassOf(o3);
    Check(InUse(blk) &&
	  (((LMemBlockHeader *) MemDeref(blk))->LMBH_flags & LMF_AUTO_FREE),
	  "block in use not freed");
    ObjDecInUseCount(blk);		/* now */
    (void) KObjClassOf(o3);
    Check(!InUse(blk), "ObjFreeObjBlock");

    /* detach and quit run through t2's queue; t3 is detached */
    t3 = ThreadCreate(PRIORITY_STANDARD, 0, ProcessThread, 0, GEODE);
    while (!ThreadGetInfo(t3, TGIT_QUEUE_HANDLE))
	TimerSleep(1);
    fails += KObjDetach(t2, t3);

    if (kernelEventsDropped) {
	fails++;
	printf("FAIL events dropped\n");
    }
    KernelTimerExit();
    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
