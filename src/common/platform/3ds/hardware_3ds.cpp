/*
** hardware_3ds.cpp
** Creates the Nintendo 3DS video backend (citro3d).
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include "hardware.h"
#include "i_system.h"
#include "c3d_backend.h"
#include "engineerrors.h"

IVideo *Video;

void I_ShutdownGraphics()
{
	if (screen)
	{
		DFrameBuffer *s = screen;
		screen = nullptr;
		delete s;
	}
	delete Video;
	Video = nullptr;
}

void I_InitGraphics()
{
	Video = C3D_CreateVideo();
	if (Video == nullptr) I_FatalError("Failed to initialize the display");
}
