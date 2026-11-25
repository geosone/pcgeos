/*
 * soundVoiceAllocation.c -- the voice and DAC manager of the sound
 * library (soundVoiceAllocation.asm).
 *
 * Every voice and DAC of the driver has a node: the voices first, then
 * the DACs.  A node is on its free list or its active list; the active
 * list is sorted by priority, the least urgent (largest value) first,
 * so that a sound of more urgency takes the voice at the head.  The
 * owner of a node is told by setting its SC_voice[VSN_voice] to
 * NO_VOICE.  Lists link the nodes by number (the asm: by offset).
 *
 * Everything here runs inside ghal_crit (the asm: interrupts off).
 */
#include "soundLocal.h"

#define END_OF_LIST	0xffff

typedef struct {
    word		VSN_next;
    word		VSN_priority;
    SoundControl	*VSN_owner;
    word		VSN_voice;		/* the owner's voice */
} VoiceStatusNode;

typedef struct {
    word	LHT_head;
    word	LHT_tail;
} ListHeadTail;

static VoiceStatusNode *voiceBlock;
static MemHandle voiceHandle;		/* fixed, the library's */
static ListHeadTail voiceFreeList = { END_OF_LIST, END_OF_LIST };
static ListHeadTail voiceActiveList = { END_OF_LIST, END_OF_LIST };
static ListHeadTail DACFreeList = { END_OF_LIST, END_OF_LIST };
static ListHeadTail DACActiveList = { END_OF_LIST, END_OF_LIST };

/* the nodes first..first+n-1 on a free list */
static void InitList(ListHeadTail *l, word first, word n)
{
    word i;

    l->LHT_head = l->LHT_tail = END_OF_LIST;
    if (!n)
	return;
    for (i = first; i < first + n; i++)
	voiceBlock[i].VSN_next = (word) (i + 1);
    voiceBlock[first + n - 1].VSN_next = END_OF_LIST;
    l->LHT_head = first;
    l->LHT_tail = (word) (first + n - 1);
}

Boolean SoundVoiceInitialize(void)
{
    word n = (word) (driverVoices + driverDACs);

    if (!n)
	return TRUE;
    voiceHandle = MemAlloc((word) (n * sizeof(VoiceStatusNode)),
			   HF_FIXED | HF_SHARABLE, HAF_ZERO_INIT);
    if (!voiceHandle)
	return FALSE;
    HandleModifyOwner(voiceHandle, GeodeGetCodeProcessHandle());
    voiceBlock = (VoiceStatusNode *) MemDeref(voiceHandle);
    ghal_crit_enter();
    InitList(&voiceFreeList, 0, driverVoices);
    InitList(&DACFreeList, driverVoices, driverDACs);
    voiceActiveList.LHT_head = voiceActiveList.LHT_tail = END_OF_LIST;
    DACActiveList.LHT_head = DACActiveList.LHT_tail = END_OF_LIST;
    ghal_crit_leave();
    return TRUE;
}

void SoundVoiceDetach(void)
{
    if (voiceHandle) {
	MemFree(voiceHandle);
	voiceHandle = 0;
	voiceBlock = NULL;
    }
}

/* the head of the free list, else the head of the active list if it is
 * of less urgency (its owner loses it); END_OF_LIST if none */
static word GetFree(ListHeadTail *freeList, ListHeadTail *activeList,
		    word priority)
{
    ListHeadTail *l = freeList;
    word n = l->LHT_head;
    VoiceStatusNode *v;

    if (!voiceBlock)
	return END_OF_LIST;
    if (n == END_OF_LIST) {
	l = activeList;
	n = l->LHT_head;
	if (n == END_OF_LIST || !(priority < voiceBlock[n].VSN_priority))
	    return END_OF_LIST;
	v = &voiceBlock[n];
	if (v->VSN_owner)
	    v->VSN_owner->SC_voice[v->VSN_voice].SVS_physicalVoice = NO_VOICE;
    }
    v = &voiceBlock[n];
    v->VSN_priority = priority;
    l->LHT_head = v->VSN_next;
    v->VSN_next = END_OF_LIST;
    if (l->LHT_tail == n)
	l->LHT_tail = END_OF_LIST;
    return n;
}

static void Free(ListHeadTail *l, word n)
{
    if (l->LHT_tail == END_OF_LIST)
	l->LHT_head = n;
    else
	voiceBlock[l->LHT_tail].VSN_next = n;
    l->LHT_tail = n;
}

/* into the active list, before the first of more urgency (a smaller
 * value), after those of the same: the head is the least urgent and the
 * oldest.  (The asm compared the dgroup segment, not the priority: ax
 * was overwritten after it was pushed; this is what it meant.) */
static void Activate(ListHeadTail *l, word n)
{
    word prio = voiceBlock[n].VSN_priority, prev = END_OF_LIST;
    word cur = l->LHT_head;

    while (cur != END_OF_LIST && !(voiceBlock[cur].VSN_priority < prio)) {
	prev = cur;
	cur = voiceBlock[cur].VSN_next;
    }
    voiceBlock[n].VSN_next = cur;
    if (prev == END_OF_LIST)
	l->LHT_head = n;
    else
	voiceBlock[prev].VSN_next = n;
    if (cur == END_OF_LIST)
	l->LHT_tail = n;
}

/* out of the active list; TRUE if it was not there */
static Boolean Deactivate(ListHeadTail *l, word n)
{
    word prev = END_OF_LIST, cur = l->LHT_head;

    while (cur != n) {
	if (cur == END_OF_LIST)
	    return TRUE;
	prev = cur;
	cur = voiceBlock[cur].VSN_next;
    }
    if (prev == END_OF_LIST)
	l->LHT_head = voiceBlock[n].VSN_next;
    else
	voiceBlock[prev].VSN_next = voiceBlock[n].VSN_next;
    voiceBlock[n].VSN_next = END_OF_LIST;
    if (l->LHT_tail == n)
	l->LHT_tail = prev;
    return FALSE;
}

static void Assign(word n, SoundControl *owner, word streamVoice, word phys)
{
    if (owner)
	owner->SC_voice[streamVoice].SVS_physicalVoice = phys;
    voiceBlock[n].VSN_voice = streamVoice;
    voiceBlock[n].VSN_owner = owner;
}

/* ------------------------------------------------------------ voices */

word SoundVoiceGetFree(word priority)
{
    word n;

    ghal_crit_enter();
    n = GetFree(&voiceFreeList, &voiceActiveList, priority);
    ghal_crit_leave();
    return n == END_OF_LIST ? NO_VOICE : n;
}

void SoundVoiceFree(word voice)
{
    if (voice >= driverVoices)
	return;
    ghal_crit_enter();
    Free(&voiceFreeList, voice);
    ghal_crit_leave();
}

void SoundVoiceActivate(word voice, word priority)
{
    if (voice >= driverVoices)
	return;
    ghal_crit_enter();
    voiceBlock[voice].VSN_priority = priority;
    Activate(&voiceActiveList, voice);
    ghal_crit_leave();
}

Boolean SoundVoiceDeactivate(word voice)
{
    Boolean r;

    if (voice >= driverVoices)
	return TRUE;
    ghal_crit_enter();
    r = Deactivate(&voiceActiveList, voice);
    ghal_crit_leave();
    return r;
}

void SoundVoiceAssign(word voice, SoundControl *owner, word streamVoice)
{
    if (voice >= driverVoices)
	return;
    ghal_crit_enter();
    Assign(voice, owner, streamVoice, voice);
    ghal_crit_leave();
}

/* -------------------------------------------------------------- DACs */

word SoundDACGetFree(word priority)
{
    word n;

    ghal_crit_enter();
    n = GetFree(&DACFreeList, &DACActiveList, priority);
    ghal_crit_leave();
    return n == END_OF_LIST ? NO_VOICE : (word) (n - driverVoices);
}

void SoundDACFree(word dac)
{
    if (dac >= driverDACs)
	return;
    ghal_crit_enter();
    Free(&DACFreeList, (word) (dac + driverVoices));
    ghal_crit_leave();
}

/* (with the priority GetFree gave it, as the asm) */
void SoundDACActivate(word dac)
{
    if (dac >= driverDACs)
	return;
    ghal_crit_enter();
    Activate(&DACActiveList, (word) (dac + driverVoices));
    ghal_crit_leave();
}

Boolean SoundDACDeactivate(word dac)
{
    Boolean r;

    if (dac >= driverDACs)
	return TRUE;
    ghal_crit_enter();
    r = Deactivate(&DACActiveList, (word) (dac + driverVoices));
    ghal_crit_leave();
    return r;
}

void SoundDACAssign(word dac, SoundControl *owner, word streamVoice)
{
    if (dac >= driverDACs)
	return;
    ghal_crit_enter();
    Assign((word) (dac + driverVoices), owner, streamVoice, dac);
    ghal_crit_leave();
}

/* ------------------------------------------------- the exported API */

static SoundControl *OwnerOf(MemHandle mh)
{
    return mh ? (SoundControl *) MemDeref(mh) : NULL;
}

word SoundVoiceManagerGetFree(SoundPriority priority)
{
    return SoundVoiceGetFree((word) priority);
}

void SoundVoiceManagerFree(word voice)
{
    SoundVoiceFree(voice);
}

void SoundVoiceManagerActivate(word voice, word priority)
{
    SoundVoiceActivate(voice, priority);
}

Boolean SoundVoiceManagerDeactivate(word voice)
{
    return SoundVoiceDeactivate(voice);
}

void SoundVoiceManagerAssign(word voice, MemHandle owner, word streamVoice)
{
    SoundVoiceAssign(voice, OwnerOf(owner), streamVoice);
}

word SoundDACManagerGetFree(SoundPriority priority)
{
    return SoundDACGetFree((word) priority);
}

void SoundDACManagerFree(word dac)
{
    SoundDACFree(dac);
}

void SoundDACManagerActivate(word dac)
{
    SoundDACActivate(dac);
}

Boolean SoundDACManagerDeactivate(word dac)
{
    return SoundDACDeactivate(dac);
}

void SoundDACManagerAssign(word dac, MemHandle owner, word streamVoice)
{
    SoundDACAssign(dac, OwnerOf(owner), streamVoice);
}
