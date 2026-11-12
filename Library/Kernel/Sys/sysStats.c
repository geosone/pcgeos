/*
 * sysStats.c -- SysGetInfo and SysStatistics (after sysStats.asm).
 *
 * The C kernel has no DOS heap: the heap size and the largest free block
 * report 0xffff paragraphs (a megabyte, "plenty"); the physical memory
 * comes from the GHAL when it knows it.  Values with no meaning in Port32
 * (segments, the loader variables, BIOS lock, text modes) are 0.
 * SysStatistics counts context switches and interrupts (timer ticks) in
 * the last second; there is no swapping.  Not yet: the number of volumes
 * and the counter at the last input.
 */
#include "kernelC.h"
#include <sysstats.h>

#define SGIT_COUNTER_AT_LAST_INPUT	24	/* sysstats.def, not in .h */
#define SGIT_SCREEN_SAVER_DELAY		26
#define SGIT_PHYS_MEM_TOTAL		28
#define SGIT_PHYS_MEM_USED		30
#define SGIT_XIP_HEADER_SEGMENT		0x8018
#define SGIT_SWAP_FREE_SIZE		0x801A

/* CPU speed as ratio to an XT * 10 (timerInit.asm measured it) */
#define PORT32_CPU_SPEED	10000

dword SysGetInfo(SysGetInfoType info)
{
    word total, procs, libs, drvs;

    switch ((word) info) {
    case SGIT_TOTAL_HANDLES:
	return HandleCountTotal();
    case SGIT_HEAP_SIZE:
    case SGIT_LARGEST_FREE_BLOCK:
	return 0xffff;
    case SGIT_TOTAL_COUNT:
	return KernelTicks();
    case SGIT_TOTAL_GEODES:
    case SGIT_NUMBER_OF_PROCESSES:
    case SGIT_NUMBER_OF_LIBRARIES:
    case SGIT_NUMBER_OF_DRIVERS:
	GeodeCount(&total, &procs, &libs, &drvs);
	return info == SGIT_TOTAL_GEODES ? total :
	       info == SGIT_NUMBER_OF_PROCESSES ? procs :
	       info == SGIT_NUMBER_OF_LIBRARIES ? libs : drvs;
    case SGIT_CPU_SPEED:
	return PORT32_CPU_SPEED;
    case SGIT_SYSTEM_DISK:
	return FileTopDisk();
    case SGIT_UI_PROCESS:
	return kernelUIHandle;
    case SGIT_SCREEN_SAVER_DELAY:
	return ImGetScreenSaverDelay();
    case SGIT_NUMBER_OF_FREE_HANDLES:
	return HandleCountTotal() - HandleCountUsed();
    case SGIT_LAST_HANDLE:
	return HandleCountTotal() * HANDLE_GRANULE;
    case SGIT_ERROR:
	return 0xffff;			/* no error */
    case SGIT_NUMBER_OF_VOLUMES:
    case SGIT_COUNTER_AT_LAST_INPUT:
    case SGIT_PHYS_MEM_TOTAL:
    case SGIT_PHYS_MEM_USED:
    case SGIT_HANDLE_TABLE_START:
    case SGIT_HANDLE_TABLE_SEGMENT:
    case SGIT_BIOS_LOCK:
    case SGIT_INITIAL_TEXT_MODE:
    case SGIT_DEFAULT_SIMPLE_GRAPHICS_MODE:
    case SGIT_CURRENT_SIMPLE_GRAPHICS_MODE:
    case SGIT_LAST_DISK_ACCESS:
    case SGIT_LOADER_VARS_ADDRESS:
    case SGIT_KCODE_SEGMENT:
    case SGIT_XIP_HEADER_SEGMENT:
    case SGIT_SWAP_FREE_SIZE:
	return 0;
    default:
	EC_CHECK(FALSE, SYS_GET_INFO_BAD_PARAMETER);
	return 0;
    }
}

void SysStatistics(SysStats *stats)
{
    KernelGetStatistics(stats);
}
