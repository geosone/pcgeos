##############################################################################
#
# sound32.gp -- the sound library, Port32 (C) build (soundLocal.h).
#
# The entry numbers are those of sound.gp.  The asm routines took
# registers; their C stubs (SOUNDALLOCMUSIC ...) and the old names
# (SoundAllocSimple ...) are the C routines under further entries.  The
# library is no driver any more: its driver functions (DR_SOUND_*) were
# its own business.  The stream driver is a library in C ("library
# stream" for glue's "driver stream").
#
##############################################################################
name	sound.lib
type	library, single
entry	SoundEntry
library	geos
library	stream
longname        "Sound Library"
tokenchars      "SNDL"
tokenid         0
nosort
export	SoundGetExclusive
export	SoundGetExclusiveNB
export	SoundReleaseExclusive
export	SoundAllocMusic
export	SoundPlayMusic
export	SoundStopMusic
export	SoundReallocMusic
export	SoundFreeMusic
export	SoundInitMusic
export	SoundAllocMusicNote
export	SoundPlayMusicNote
export	SoundStopMusicNote
export	SoundReallocMusicNote
export	SoundFreeMusicNote
export	SoundAllocMusicStream
export	SoundStopMusicStream
export	SoundFreeMusicStream
export	SoundPlayToMusicStream
export	SoundChangeOwnerSimple as SoundChangeOwnerMusic
export	SoundChangeOwnerStream
export	SoundAllocSampleStream
export	SoundFreeSampleStream
export	SoundPlayToSampleStream
export	SoundEnableSampleStream
export	SoundDisableSampleStream
export	SoundVoiceManagerGetFree
export	SoundVoiceManagerFree
export	SoundVoiceManagerActivate
export	SoundVoiceManagerDeactivate
export	SoundVoiceManagerAssign
export	SoundDACManagerGetFree
export	SoundDACManagerFree
export	SoundDACManagerActivate
export	SoundDACManagerDeactivate
export	SoundDACManagerAssign
export	SoundSynthDriverInfo
export	SoundSampleDriverInfo
export	SoundGetExclusive as SOUNDGETEXCLUSIVE
export	SoundGetExclusiveNB as SOUNDGETEXCLUSIVENB
export	SoundReleaseExclusive as SOUNDRELEASEEXCLUSIVE
export	SoundAllocMusic as SOUNDALLOCMUSIC
export	SoundPlayMusic as SOUNDPLAYMUSIC
export	SoundStopMusic as SOUNDSTOPMUSIC
export	SoundReallocMusic as SOUNDREALLOCMUSIC
export	SoundFreeMusic as SOUNDFREEMUSIC
export	SoundAllocMusicNote as SOUNDALLOCMUSICNOTE
export	SoundPlayMusicNote as SOUNDPLAYMUSICNOTE
export	SoundStopMusicNote as SOUNDSTOPMUSICNOTE
export	SoundReallocMusicNote as SOUNDREALLOCMUSICNOTE
export	SoundFreeMusicNote as SOUNDFREEMUSICNOTE
export	SoundAllocMusicStream as SOUNDALLOCMUSICSTREAM
export	SoundPlayToMusicStream as SOUNDPLAYTOMUSICSTREAM
export	SoundStopMusicStream as SOUNDSTOPMUSICSTREAM
export	SoundFreeMusicStream as SOUNDFREEMUSICSTREAM
export	SoundAllocSampleStream as SOUNDALLOCSAMPLESTREAM
export	SoundPlayToSampleStream as SOUNDPLAYTOSAMPLESTREAM
export	SoundEnableSampleStream as SOUNDENABLESAMPLESTREAM
export	SoundDisableSampleStream as SOUNDDISABLESAMPLESTREAM
export	SoundFreeSampleStream as SOUNDFREESAMPLESTREAM
export	SoundChangeOwnerStream as SOUNDCHANGEOWNERSTREAM
export	SoundChangeOwnerSimple as SOUNDCHANGEOWNERSIMPLE
export	SoundInitMusic as SOUNDINITMUSIC
export	SoundSynthDriverInfo as SOUNDSYNTHDRIVERINFO
export	SoundSampleDriverInfo as SOUNDSAMPLEDRIVERINFO
export	SoundVoiceManagerGetFree as SOUNDVOICEMANAGERGETFREE
export	SoundVoiceManagerFree as SOUNDVOICEMANAGERFREE
skip 3
export	SoundDACManagerGetFree as SOUNDDACMANAGERGETFREE
export	SoundDACManagerFree as SOUNDDACMANAGERFREE
skip 3
export	SoundAllocMusic as SoundAllocSimple
export	SoundPlayMusic as SoundPlaySimple
export	SoundStopMusic as SoundStopSimple
export	SoundReallocMusic as SoundReallocSimple
export	SoundFreeMusic as SoundFreeSimple
export	SoundInitMusic as SoundInitSimple
export	SoundAllocMusic as SoundAllocSimpleFM
export	SoundPlayMusic as SoundPlaySimpleFM
export	SoundStopMusic as SoundStopSimpleFM
export	SoundReallocMusic as SoundReallocSimpleFM
export	SoundFreeMusic as SoundFreeSimpleFM
export	SoundInitMusic as SoundInitSimpleFM
export	SoundAllocMusicNote as SoundAllocNote
export	SoundPlayMusic as SoundPlayNote
export	SoundStopMusic as SoundStopNote
export	SoundReallocMusicNote as SoundReallocNote
export	SoundFreeMusic as SoundFreeNote
export	SoundChangeOwnerSimple as SoundChangeOwner
export	SoundChangeOwnerSimple
export	SoundPlayMusicLMem
export	SoundStopMusicLMem
export	SoundPlayToMusicStreamNB
export	SoundPlayMusicLMem as SOUNDPLAYMUSICLMEM
export	SoundStopMusicLMem as SOUNDSTOPMUSICLMEM
export	SoundPlayToMusicStreamNB as SOUNDPLAYTOMUSICSTREAMNB
export	SoundReallocMusicLMem
export	SoundReallocMusicLMem as SOUNDREALLOCMUSICLMEM
export	SoundMixerGetMasterVolume
export	SoundMixerSetMasterVolume
export	SoundMixerGetSourceCount
export	SoundMixerMapSource
export	SoundMixerGetSourceVolume
export	SoundMixerSetSourceVolume
export	SoundMixerGetEffectCount
export	SoundMixerMapEffect
export	SoundMixerGetEffectLevel
export	SoundMixerSetEffectLevel
export	SoundMixerGetMasterVolume as SOUNDMIXERGETMASTERVOLUME
export	SoundMixerSetMasterVolume as SOUNDMIXERSETMASTERVOLUME
