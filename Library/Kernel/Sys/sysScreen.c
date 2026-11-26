/*
 * sysScreen.c -- after sysScreen.asm: SysRegisterScreen.
 */
#include "kernelC.h"
#include "ghal.h"
#include <system.h>

#define MAX_ERROR_SCREENS	4

static struct {
    GeodeHandle		driver;
    WindowHandle	root;
} errorScreens[MAX_ERROR_SCREENS];

void SysRegisterScreen(GeodeHandle driver, WindowHandle root)
{
    word i;

    for (i = 0; i < MAX_ERROR_SCREENS; i++)
	if (!errorScreens[i].root || errorScreens[i].root == root) {
	    errorScreens[i].driver = driver;
	    errorScreens[i].root = root;
	    return;
	}
}
