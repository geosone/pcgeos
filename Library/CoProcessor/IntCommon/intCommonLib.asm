COMMENT @%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

	Copyright (c) GeoWorks 1991 -- All Rights Reserved

PROJECT:	PC GEOS
MODULE:		
FILE:		intCommonLib.asm

AUTHOR:		jimmy lefkowitz

REVISION HISTORY:
	Name	Date		Description
	----	----		-----------
	jimmy	4/20/92		Initial version.

DESCRIPTION:
	

	$Id: intCommonLib.asm,v 1.1 97/04/04 17:48:32 newdeal Exp $

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%@


InitCode	segment	resource

COMMENT @%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
		Intel80X87LibraryEntry
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

SYNOPSIS:	sees if the chip is really there...

CALLED BY:	GLOBAL

PASS:		nothing

RETURN:		carry set if no coprocessor

DESTROYED:	Nada.

PSEUDOCODE/STRATEGY:	

KNOWN BUGS/SIDEFFECTS/IDEAS:

REVISION HISTORY:
	Name	Date		Description
	----	----		-----------
	jimmy	4/20/92		Initial version.

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%@

ifndef PROTECTED_MODE
NDP_STATUS	dw	-1
endif

public Intel80X87LibraryEntry

Intel80X87LibraryEntry	proc	far
	uses	ds
	.enter
	fninit
ifdef PROTECTED_MODE
	;
	; The status word goes to the stack: under protected mode (GEOS32)
	; a real CPU does not write through the code selector.
	;
	push	bp
	push	ax			; scratch word for fnstsw/fnstcw
	mov	bp, sp
	fnstsw	{word}ss:[bp]

	; if we can't write the status word, no processor
	tst	{byte}ss:[bp]
	jnz	noCoproc

	; next, we check to see if a valid control word can be written
	; if not, no CoProcessor is present. Don't use WAIT forms!!!

	fnstcw	{word}ss:[bp]
	and	{word}ss:[bp], 103fh
	cmp	{word}ss:[bp], 3fh	; correct value
	jne	noCoproc
	clc
done:
	pop	ax			; (pop leaves the carry alone)
	pop	bp
	.leave
	ret
else
	segmov	ds, cs
	fnstsw	{word}ds:[NDP_STATUS]

	; if we can't write the status word, no processor
	tst	{byte}ds:[NDP_STATUS]
	jnz	noCoproc		
	
	; next, we check to see if a valid control word can be written
	; if not, no CoProcessor is present. Don't use WAIT forms!!!

	fnstcw	{word}ds:[NDP_STATUS]
	and	{word}ds:[NDP_STATUS], 103fh
	cmp	{word}ds:[NDP_STATUS], 3fh	; correct value
	jne	noCoproc
	clc
done:
	.leave
	ret
endif
noCoproc:
	stc
	jmp	done
Intel80X87LibraryEntry	endp

InitCode	ends





