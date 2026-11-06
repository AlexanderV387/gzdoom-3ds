/*
** widgets_3ds.cpp
** The Nintendo 3DS has no desktop windows (ZWidget): the launcher, the
** network start window and the theme become no-ops. Also the few POSIX
** calls newlib lacks.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <errno.h>
#include <malloc.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>

#include "c_cvars.h"
#include "zstring.h"
#include "widgets/netstartwindow.h"
#include "widgets/themedata.h"

CVAR(Int, ui_preferred_theme, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

Colorf Theme::accent;

void InitWidgetResources(const char *basewad) {}
void CloseWidgetResources() {}

void UpdateVRModes(bool considerQuadBuffered) {}
void PrintVRAM_NV(FString &out) {}
void PrintVRAM_ATI(FString &out) {}

void NetStartWindow::NetInit(const char *message, bool host) {}
void NetStartWindow::NetMessage(const char *message) {}
void NetStartWindow::NetConnect(int client, const char *name, unsigned flags, int status) {}
void NetStartWindow::NetUpdate(int client, int status) {}
void NetStartWindow::NetDisconnect(int client) {}
void NetStartWindow::NetProgress(int cur, int limit) {}
void NetStartWindow::NetDone() {}
void NetStartWindow::NetClose() {}
bool NetStartWindow::ShouldStartNet() { return false; }
int NetStartWindow::GetNetKickClient() { return -1; }
int NetStartWindow::GetNetBanClient() { return -1; }
bool NetStartWindow::NetLoop(bool (*timer_callback)(void *), void *userdata) { return false; }

extern "C"
{
	uid_t getuid(void) { return 0; }
	struct passwd *getpwuid(uid_t) { return nullptr; }
	struct passwd *getpwnam(const char *) { return nullptr; }
	int posix_memalign(void **memptr, size_t alignment, size_t size)
	{
		*memptr = memalign(alignment, size);
		return *memptr ? 0 : ENOMEM;
	}
}

// No window on the 3DS (the SDL desktop code would set its title and handle its events).
#include <SDL2/SDL.h>
void I_SetWindowTitle(const char *caption) {}
void ProcessSDLWindowEvent(const SDL_WindowEvent &event) {}
