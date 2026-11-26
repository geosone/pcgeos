/*
 * soundC.c -- after soundC.asm: C-stubs for the library interface. For
 * more information, see the regular assemply routines.
 */
#include "soundLocal.h"
#include <string.h>

word SoundStopMusicLMem(MemHandle mh)
{
    return SoundStopMusic(mh);
}

word SoundPlayMusicNote(MemHandle mh, SoundPriority priority, word tempo,
			EndOfSongFlags flags)
{
    return SoundPlayMusic(mh, (word) priority, tempo, flags);
}

word SoundStopMusicNote(MemHandle mh)
{
    return SoundStopMusic(mh);
}

void SoundFreeMusicNote(MemHandle mh)
{
    (void) SoundFreeSimple(mh);
}

void SoundChangeOwnerSimple(MemHandle mh, MemHandle owner)
{
    SoundChangeOwnerBlock(mh, (GeodeHandle) owner);
}

word SoundVoiceManagerGetFree(SoundPriority priority)
{
    return SoundVoiceGetFree((word) priority);
}

void SoundVoiceManagerFree(word voice)
{
    SoundVoiceFree(voice);
}

word SoundDACManagerGetFree(SoundPriority priority)
{
    return SoundDACGetFree((word) priority);
}

void SoundDACManagerFree(word dac)
{
    SoundDACFree(dac);
}
