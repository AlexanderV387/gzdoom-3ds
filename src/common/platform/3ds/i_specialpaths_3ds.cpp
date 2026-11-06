/*
** i_specialpaths_3ds.cpp
** Where the Nintendo 3DS port keeps its files: everything in one folder
** on the SD card (configuration, saves, screenshots, demos, cache).
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <sys/stat.h>
#include <stdlib.h>

#include "cmdlib.h"
#include "i_specialpaths.h"
#include "platform_3ds.h"

static FString AppFolder(const char *sub)
{
	FString path = N3DS_DATA_DIR;
	if (sub && *sub) { path += "/"; path += sub; }
	CreatePath(path.GetChars());
	return path;
}

const char *GetConfigPath() { return N3DS_DATA_DIR; }
const char *GetCachePath() { return N3DS_DATA_DIR "/cache"; }
const char *GetDataPath() { return N3DS_DATA_DIR; }

FString GetUserFile(const char *file)
{
	return AppFolder(nullptr) + "/" + file;
}

FString M_GetAppDataPath(bool create)
{
	return create ? AppFolder(nullptr) : FString(N3DS_DATA_DIR);
}

FString M_GetCachePath(bool create, FString ns)
{
	FString path = FString(N3DS_DATA_DIR "/cache/") + ns;
	if (create) CreatePath(path.GetChars());
	return path;
}

FString M_GetAutoexecPath()
{
	return N3DS_DATA_DIR "/autoexec.cfg";
}

FString M_GetConfigPath(bool for_reading)
{
	return N3DS_DATA_DIR "/" GAMENAMELOWERCASE ".ini";
}

FString M_GetDocumentsPath()
{
	return AppFolder(nullptr) + "/";
}

FString M_GetScreenshotsPath()
{
	return AppFolder("screenshots") + "/";
}

FString M_GetSavegamesPath()
{
	return AppFolder("saves") + "/";
}

FString M_GetDemoPath()
{
	return AppFolder("demos") + "/";
}

FString M_GetNormalizedPath(const char *path)
{
	return path;
}
