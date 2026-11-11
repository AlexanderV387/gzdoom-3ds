/*
** i_system_3ds.cpp
** System services for the Nintendo 3DS: console output, timers, random
** seed, fatal errors. Based on the SDL/POSIX version, without the desktop
** parts (terminal, windows, clipboard, launcher).
**
** Copyright 1999-2016 Randy Heit, 2019-2025 the GZDoom/UZDoom contributors,
** 2026 AlexanderV387 (Nintendo 3DS port).
** GPL v3 (see LICENSE).
*/

#include <3ds.h>
#include <SDL2/SDL.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "c_cvars.h"
#include "i_interface.h"
#include "i_system.h"
#include "m_argv.h"
#include "printf.h"
#include "st_start.h"
#include "v_font.h"
#include "vm.h"
#include "platform_3ds.h"

CVAR(String, queryiwad_key, "shift", CVAR_GLOBALCONFIG | CVAR_ARCHIVE);
CVAR(Bool, con_printansi, false, CVAR_GLOBALCONFIG | CVAR_ARCHIVE);
CVAR(Bool, con_4bitansi, false, CVAR_GLOBALCONFIG | CVAR_ARCHIVE);
EXTERN_CVAR(Bool, longsavemessages)

double PerfToSec, PerfToMillisec;
bool PerfAvailable;

void I_SetIWADInfo()
{
}

extern "C" int I_FileAvailable(const char *filename)
{
	return access(filename, F_OK) == 0;
}

bool SDL_I_CheckForRestart(void)
{
	return false;
}

void I_ShowFatalError(const char *message)
{
	N3DS_FatalError(message);
}

void CalculateCPUSpeed()
{
	// The ARM11's system tick: 268 MHz on both models.
	PerfAvailable = true;
	PerfToSec = 1.0 / SYSCLOCK_ARM11;
	PerfToMillisec = PerfToSec * 1000.;
}

void CleanProgressBar()
{
}

void RedrawProgressBar(int CurPos, int MaxPos)
{
}

void I_PrintStr(const char *cp)
{
	// Plain text into the log: the engine's color escapes are dropped.
	FString printData;
	for (const char *srcp = cp; *srcp != 0;)
	{
		if (*srcp == 0x1c)
		{
			srcp++;
			if (*srcp == '[') { while (*srcp && *srcp != ']') srcp++; if (*srcp) srcp++; }
			else if (*srcp) srcp++;
		}
		else if (*srcp == 0x1d || *srcp == 0x1e || *srcp == 0x1f) srcp++;
		else printData += *srcp++;
	}
	// Each new line starts with the elapsed time (ms) and the heap in use (KB):
	// when each phase runs and where the memory goes while loading.
	static bool lineStart = true;
	if (lineStart && printData.Len() > 0)
	{
		char prefix[64];
		snprintf(prefix, sizeof(prefix), "[T+%6lld  heap=%5uKB] ", N3DS_ElapsedMs(), N3DS_HeapInUseKB());
		N3DS_Log(prefix);
	}
	N3DS_Log(printData.GetChars());
	if (printData.Len() > 0) lineStart = printData.Back() == '\n';

	// Onto the SD card at most every quarter of a second: a hang or a power-off loses little.
	static u64 lastSync = 0;
	const u64 now = svcGetSystemTick();
	if (lineStart && now - lastSync > SYSCLOCK_ARM11 / 4)
	{
		N3DS_LogFlush();
		lastSync = now;
	}
}

bool HoldingQueryKey(const char *key)
{
	return false;
}

bool I_PickIWad(bool showwin, FStartupSelectionInfo &info)
{
	// The IWAD and the mods are chosen before the engine starts (i_main_3ds.cpp).
	return true;
}

void I_PutInClipboard(const char *str)
{
}

FString I_GetFromClipboard(bool use_primary_selection)
{
	return "";
}

FString I_GetCWD()
{
	char *curdir = getcwd(NULL, 0);
	if (!curdir) return "";
	FString ret(curdir);
	free(curdir);
	return ret;
}

bool I_ChDir(const char *path)
{
	return chdir(path) == 0;
}

unsigned int I_MakeRNGSeed()
{
	return (unsigned int)(svcGetSystemTick() ^ time(NULL));
}

void I_OpenShellFolder(const char *infolder)
{
	if (longsavemessages) Printf("Files are in %s\n", infolder);
}
