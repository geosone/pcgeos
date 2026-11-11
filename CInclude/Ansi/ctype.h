/*********************************************************************
 *								     
 *	Copyright (c) GeoWorks 1991 -- All Rights Reserved	     
 *								     
 * 	PROJECT:	PC GEOS					     
 * 	MODULE:							     
 * 	FILE:		ctype.h 				     
 *								     
 *	AUTHOR:		jimmy lefkowitz				     
 *								     
 *	REVISION HISTORY:					     
 *								     
 *	Name	Date		Description			     
 *	----	----		-----------			     
 *	jimmy	8/15/91		Initial              		     
 *	schoon	4/8/92		Updated to Ansi C standards   	     
 *							   	     
 *	DESCRIPTION:						     
 *								     
 *	$Id: ctype.h,v 1.1 97/04/04 15:50:25 newdeal Exp $
 *							   	     
 *********************************************************************/
#ifndef __CTYPE_H
#define __CTYPE_H

#ifdef GEOS_PORT32
/*
 * Port32: a geode imports these from the GEOS C library like any other
 * routine.  The prototypes are the standard ones, so they agree with the
 * compiler's built-ins and with a host <ctype.h> (Port32/ABI.md).
 */
extern int isalnum(int __c);
extern int isalpha(int __c);
extern int iscntrl(int __c);
extern int isdigit(int __c);
extern int isgraph(int __c);
extern int islower(int __c);
extern int isprint(int __c);
extern int ispunct(int __c);
extern int isspace(int __c);
extern int isupper(int __c);
extern int isxdigit(int __c);
extern int toupper(int __c);
extern int tolower(int __c);
#else

extern int	
  _pascal isalnum(unsigned int __c);

extern int	
  _pascal isalpha(unsigned int __c);

extern int 	
  _pascal iscntrl(unsigned int __c);

extern int	
  _pascal isdigit(unsigned int __c);

extern int	
  _pascal isgraph(unsigned int __c);

extern int	
  _pascal islower(unsigned int __c);

extern int	
  _pascal isprint(unsigned int __c);

extern int	
  _pascal ispunct(unsigned int __c);

extern int	
  _pascal isspace(unsigned int __c);

extern int	
  _pascal isupper(unsigned int __c);

extern int	
  _pascal isxdigit(unsigned int __c);

extern int
  _pascal toupper(unsigned int __c);

extern int
  _pascal tolower(unsigned int __c);
  

#ifdef __HIGHC__
pragma Alias(isupper, "ISUPPER");
pragma Alias(islower, "ISLOWER");
pragma Alias(isalpha, "ISALPHA");
pragma Alias(ispunct, "ISPUNCT");
pragma Alias(isspace, "ISSPACE");
pragma Alias(iscntrl, "ISCNTRL");
pragma Alias(isdigit, "ISDIGIT");
pragma Alias(isxdigit, "ISXDIGIT");
pragma Alias(isalnum, "ISALNUM");
pragma Alias(isprint, "ISPRINT");
pragma Alias(isgraph, "ISGRAPH");
pragma Alias(toupper, "TOUPPER");
pragma Alias(tolower, "TOLOWER");
#endif

#endif /* GEOS_PORT32 */

#endif























