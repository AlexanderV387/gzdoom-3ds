/*
** v_palette.cpp
**
** Automatic colormap generation for "colored lights", etc.
**
**---------------------------------------------------------------------------
**
** Copyright 1998-2016 Marisa Heit
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/

#include "g_level.h"


#include "v_video.h"
#include "filesystem.h"
#include "i_video.h"
#include "c_dispatch.h"
#include "st_stuff.h"
#include "g_levellocals.h"
#include "m_png.h"
#include "v_colortables.h"
#include "printf.h"
#include "common/utility/i_time.h"

using namespace FileSys;

/* Current color blending values */
int		BlendR, BlendG, BlendB, BlendA;



void InitPalette ()
{
	uint8_t pal[768];

	ReadPalette(fileSystem.GetNumForName("PLAYPAL"), pal);
	Printf("[load] ReadPalette T+%llu\n", I_msTime());

	GPalette.Init(NUM_TRANSLATION_TABLES, nullptr);
	Printf("[load] GPalette.Init T+%llu\n", I_msTime());
	GPalette.SetPalette (pal, -1);
	Printf("[load] GPalette.SetPalette T+%llu\n", I_msTime());

	int lump = fileSystem.CheckNumForName("COLORMAP");
	if (lump == -1) lump = fileSystem.CheckNumForName("COLORMAP", ns_colormaps);
	if (lump != -1)
	{
		FileData cmap = fileSystem.ReadFile(lump);
		auto cmapdata = cmap.bytes();
		GPalette.GenerateGlobalBrightmapFromColormap(cmapdata, 32);
		Printf("[load] GenerateGlobalBrightmapFromColormap T+%llu\n", I_msTime());
		MakeGoodRemap((uint32_t*)GPalette.BaseColors, GPalette.Remap, cmapdata + 7936);	// last entry in colormap
		Printf("[load] MakeGoodRemap T+%llu\n", I_msTime());
	}
	else
		MakeGoodRemap ((uint32_t*)GPalette.BaseColors, GPalette.Remap);
		Printf("[load] MakeGoodRemap T+%llu\n", I_msTime());

	ColorMatcher.SetPalette ((uint32_t *)GPalette.BaseColors);
	Printf("[load] ColorMatcher.SetPalette T+%llu\n", I_msTime());

	if (GPalette.Remap[0] == 0)
	{ // No duplicates, so settle for something close to color 0
		GPalette.Remap[0] = BestColor ((uint32_t *)GPalette.BaseColors,
			GPalette.BaseColors[0].r, GPalette.BaseColors[0].g, GPalette.BaseColors[0].b, 1, 255);
	}
	GPalette.BaseColors[0] = 0;

	// Colormaps have to be initialized before actors are loaded,
	// otherwise Powerup.Colormap will not work.
	R_InitColormaps ();
	Printf("[load] R_InitColormaps T+%llu\n", I_msTime());
	BuildTransTable (GPalette.BaseColors);
	Printf("[load] BuildTransTable T+%llu\n", I_msTime());

}
