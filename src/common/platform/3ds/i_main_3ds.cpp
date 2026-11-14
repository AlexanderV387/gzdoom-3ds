/*
** i_main_3ds.cpp
** The Nintendo 3DS port's entry point: New 3DS speed, the SD card folder, the
** log, the game and mods from the SD card, then the engine.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <3ds.h>

#include <dirent.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>

#include "cmdlib.h"
#include "engineerrors.h"
#include "i_interface.h"
#include "i_system.h"
#include "m_argv.h"
#include "printf.h"
#include "version.h"
#include "zstring.h"
#include "platform_3ds.h"

// The test build's number and commit, written by platform/3ds/build-test.sh: every log says
// which code it comes from.
#if __has_include("n3ds_testinfo.h")
#include "n3ds_testinfo.h"
#else
#define N3DS_TEST_ID "dev build (not made with build-test.sh)"
#endif

extern const char *const BACKEND = "3DS";

FString sys_ostype = "Nintendo 3DS";
FArgs *Args;

int GameMain();
void I_StartupJoysticks();

// More memory for the application on the New 3DS (124 MB mode), set in the CIA's metadata;
// the 3DSX gets it from the Homebrew Launcher.
u32 __stacksize__ = 1024 * 1024;

static FILE *LogFile;
static u64 g_StartTick;
static aptHookCookie g_AptHookCookie;

static void N3DS_AtexitCallback()
{
	if (FILE *f = fopen(N3DS_DATA_DIR "/log.txt", "a"))
	{
		fputs("[EXIT] atexit callback\n", f);
		fflush(f);
		fsync(fileno(f));
		fclose(f);
	}
}

static void N3DS_AptHookCallback(APT_HookType hookType, void *param)
{
	switch (hookType)
	{
		case APTHOOK_ONSUSPEND:
			N3DS_Log("[APT] suspend\n");
			N3DS_LogFlush();
			break;
		case APTHOOK_ONRESTORE:
			N3DS_Log("[APT] restore\n");
			N3DS_LogFlush();
			break;
		case APTHOOK_ONEXIT:
			// Log may already be closed by main(); reopen in append mode
			if (FILE *f = fopen(N3DS_DATA_DIR "/log.txt", "a"))
			{
				fputs("[APT] exit\n", f);
				fflush(f);
				fsync(fileno(f));
				fclose(f);
			}
			break;
		default:
			break;
	}
}

void N3DS_Log(const char *text)
{
	if (LogFile == nullptr) return;
	fputs(text, LogFile);
}

long long N3DS_ElapsedMs()
{
	u64 now = svcGetSystemTick();
	return (long long)((now - g_StartTick) / (SYSCLOCK_ARM11 / 1000));
}

// fflush only hands the text to the FS service; fsync (FSFILE_Flush) puts it on the SD card.
// Without it the last lines of the log can be lost when the console is switched off.
void N3DS_LogFlush()
{
	if (LogFile == nullptr) return;
	fflush(LogFile);
	fsync(fileno(LogFile));
}

void N3DS_FatalError(const char *message)
{
	if (LogFile)
	{
		fprintf(LogFile, "\nFATAL: %s\n", message);
		N3DS_LogMemory("fatal");
	}
	if (FILE *f = fopen(N3DS_DATA_DIR "/fatal.log", "w"))
	{
		fputs(message, f);
		fclose(f);
	}

	// The system's error dialog, then back to the HOME Menu.
	errorConf err;
	errorInit(&err, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
	errorText(&err, message);
	errorDisp(&err);
	exit(1);
}

FString I_DetectOS()
{
	return "Nintendo 3DS";
}

// The IWAD from N3DS_DATA_DIR (the one named in iwad.txt, else the first known one) and every
// mod in N3DS_DATA_DIR/mods, in name order.
static void AddGameArguments()
{
	static char iwadPath[512];
	struct stat st;
	iwadPath[0] = 0;
	if (FILE *chosen = fopen(N3DS_DATA_DIR "/iwad.txt", "r"))
	{
		char name[256] = "";
		if (fgets(name, sizeof(name), chosen))
		{
			name[strcspn(name, "\r\n")] = 0;
			if (name[0])
			{
				snprintf(iwadPath, sizeof(iwadPath), N3DS_DATA_DIR "/%s", name);
				if (stat(iwadPath, &st) != 0) iwadPath[0] = 0;
			}
		}
		fclose(chosen);
	}
	static const char *const IWadNames[] = {
		"doom2.wad", "doom.wad", "plutonia.wad", "tnt.wad", "doom2f.wad",
		"freedoom2.wad", "freedoom1.wad", "doom1.wad", "freedm.wad",
		"heretic.wad", "heretic1.wad", "hexen.wad", "strife1.wad",
		"chex3.wad", "chex.wad", "harm1.wad", "hacx.wad", "rekkrsa.wad",
	};
	for (const char *name : IWadNames)
	{
		if (iwadPath[0]) break;
		snprintf(iwadPath, sizeof(iwadPath), N3DS_DATA_DIR "/%s", name);
		if (stat(iwadPath, &st) != 0) iwadPath[0] = 0;
	}
	if (!iwadPath[0])
	{
		N3DS_FatalError("No game found.\n\nCopy an IWAD (doom2.wad, doom.wad, freedoom2.wad...) to sdmc:/3ds/gzdoom/");
	}
	Args->AppendRawArg("-iwad");
	Args->AppendRawArg(iwadPath);
	fprintf(LogFile, "IWAD: %s\n", iwadPath);

	TArray<FString> mods, patches;
	if (DIR *dir = opendir(N3DS_DATA_DIR "/mods"))
	{
		while (dirent *entry = readdir(dir))
		{
			const char *dot = strrchr(entry->d_name, '.');
			if (entry->d_name[0] == '.' || dot == nullptr) continue;
			FString path = FStringf(N3DS_DATA_DIR "/mods/%s", entry->d_name);
			if (!strcasecmp(dot, ".deh") || !strcasecmp(dot, ".bex")) patches.Push(path);
			else if (!strcasecmp(dot, ".pk3") || !strcasecmp(dot, ".pk7") || !strcasecmp(dot, ".wad")
				|| !strcasecmp(dot, ".zip") || !strcasecmp(dot, ".ipk3")) mods.Push(path);
		}
		closedir(dir);
	}
	auto byName = [](const FString &a, const FString &b) { return a.CompareNoCase(b) < 0; };
	std::sort(mods.begin(), mods.end(), byName);
	std::sort(patches.begin(), patches.end(), byName);
	for (const FString &mod : mods)
	{
		Args->AppendRawArg("-file");
		Args->AppendRawArg(mod);
		fprintf(LogFile, "Mod: %s\n", mod.GetChars());
	}
	for (const FString &patch : patches)
	{
		Args->AppendRawArg("-deh");
		Args->AppendRawArg(patch);
		fprintf(LogFile, "Patch: %s\n", patch.GetChars());
	}
}

int main(int argc, char **argv)
{
	atexit(N3DS_AtexitCallback);
	osSetSpeedupEnable(true);	// New 3DS: 804 MHz and the L2 cache
	Result g_RomfsInitRc = romfsInit();
	mkdir("sdmc:/3ds", 0777);
	mkdir(N3DS_DATA_DIR, 0777);

	LogFile = fopen(N3DS_DATA_DIR "/log.txt", "w");
	if (LogFile) setvbuf(LogFile, nullptr, _IOLBF, 4096);	// line by line: a crash keeps the log
	fprintf(LogFile, "%s %s for Nintendo 3DS (%s)\n", GAMENAME, GetVersionString(), __DATE__);
	fprintf(LogFile, "Test: %s\n", N3DS_TEST_ID);
	// Diagnostics: RomFS mount result, homebrew flag, argv[0]
	{
		char d[256];
		snprintf(d, sizeof(d), "[ROMFS] init=%s homebrew=%d argv[0]=%s\n",
			R_SUCCEEDED(g_RomfsInitRc) ? "OK" : "FAIL",
			envIsHomebrew(),
			argc > 0 && argv[0] ? argv[0] : "(null)");
		N3DS_Log(d);
	}
	N3DS_LogFlush();

	g_StartTick = svcGetSystemTick();

	N3DS_LogMemory("start");
	fprintf(LogFile, "T+0 ms: start\n");
	gfxInitDefault();

	aptHook(&g_AptHookCookie, N3DS_AptHookCallback, NULL);

	setlocale(LC_ALL, "C");
	if (SDL_Init(0) < 0)
	{
		N3DS_FatalError(SDL_GetError());
	}

	Args = new FArgs(argc, argv);
	AddGameArguments();

	// The engine's own data (gzdoom.pk3 and game_support.pk3): in the RomFS (the CIA's, or the
	// one inside the 3DSX), else in the SD card's data folder.
	struct stat st;
	bool pk3_ok = (stat("romfs:/data/" BASEWAD, &st) == 0);
	{
		char d[128];
		snprintf(d, sizeof(d), "[ROMFS] pk3: %s\n", pk3_ok ? "YES" : "NO");
		N3DS_Log(d);
	}
	progdir = pk3_ok ? "romfs:/data/" : N3DS_DATA_DIR "/data/";
	fprintf(LogFile, "Engine data: %s\n", progdir.GetChars());
	fflush(LogFile);

	I_StartupJoysticks();

	fprintf(LogFile, "T+%lld ms: GameMain start\n", N3DS_ElapsedMs());
	fflush(LogFile);

	const int result = GameMain();

	N3DS_Log("[EXIT] after GameMain"); N3DS_LogFlush();
	fprintf(LogFile, "T+%lld ms: GameMain exit %d\n", N3DS_ElapsedMs(), result);
	N3DS_LogFlush();

	SDL_Quit();
	N3DS_Log("[EXIT] after SDL_Quit"); N3DS_LogFlush();

	gfxExit();
	N3DS_Log("[EXIT] after gfxExit"); N3DS_LogFlush();

	#if 0
	romfsExit();
	N3DS_Log("[EXIT] after romfsExit"); N3DS_LogFlush();
	#endif

	if (LogFile) fclose(LogFile);
	LogFile = nullptr;

	N3DS_Log("[EXIT] log closed, before return"); N3DS_LogFlush();
	return result;
}
