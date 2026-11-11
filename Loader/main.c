/*
 * main.c -- the program that starts GEOS on the host (after LoadGeos of
 * Loader/main.asm).
 *
 * The DOS16 loader found geos.ini in the current directory, took that
 * directory as the GEOS top, and handed its command tail to the kernel.
 * This program does the same: the top is the current directory, and the
 * arguments, joined by blanks, are the command tail (switches "/x", then
 * the name of a geode to run, from SP_SYSTEM).  It returns when no
 * process is left or GEOS ends (SysShutdown), and prints the reason
 * GEOS gave.  The exit code is 1 after a dirty exit.  A reboot or power
 * off (KEF_RESET, KEF_POWER_OFF) only ends the program.
 *
 *	geos [/switch ...] [geode]
 *
 * Not yet: the switches of the loader (/p<ini key>, /sp<path>=<dir>,
 * memory limits) and the TSR loader.
 */
#include "kernelC.h"

#include <stdio.h>
#include <string.h>

#define TAIL_SIZE 256

int main(int argc, char **argv)
{
    char tail[TAIL_SIZE];
    size_t len = 0, n;
    GeodeLoadError e;
    GeodeHandle gh;
    word flags;
    int i;

    tail[0] = 0;
    for (i = 1; i < argc; i++) {
	n = strlen(argv[i]);
	if (len + n + 2 > sizeof(tail)) {
	    fprintf(stderr, "geos: command line too long\n");
	    return 2;
	}
	if (len)
	    tail[len++] = ' ';
	memcpy(tail + len, argv[i], n + 1);
	len += n;
    }
    gh = KernelBoot(".", tail, &e);
    if (!gh && e != GLE_OK) {
	fprintf(stderr, "geos: cannot load %s (GeodeLoadError %d)\n", tail,
		(int) e);
	return 1;
    }
    flags = KernelRun();
    if (*KernelExitMessage())
	printf("%s\n", KernelExitMessage());
    return flags & KEF_DIRTY ? 1 : 0;
}
