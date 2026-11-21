/*
** v_colortables.cpp
**
** Various color blending tables
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


#include "v_colortables.h"
#include "colormatcher.h"
#include "m_crc32.h"
#include "printf.h"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

#ifdef __3DS__
#include "platform_3ds.h"
#endif


uint32_t Col2RGB8[65][256];
uint32_t *Col2RGB8_LessPrecision[65];
uint32_t Col2RGB8_Inverse[65][256];
uint32_t Col2RGB8_2[63][256]; // this array's second dimension is called up by pointer as Col2RGB8_LessPrecision[] elsewhere.
ColorTable32k RGB32k;
ColorTable256k RGB256k;





//==========================================================================
//
// BuildTransTable
//
// Build the tables necessary for blending - used by software rendering and
// texture composition
//
//==========================================================================

void BuildTransTable (const PalEntry *palette)
{
	int r, g, b;

#ifdef __3DS__
	{
		clock_t start = clock();
		uint32_t paletteCRC = CalcCRC32((const uint8_t*)palette, 256 * sizeof(PalEntry));
		char cachePath[128];
		snprintf(cachePath, sizeof(cachePath), "sdmc:/3ds/gzdoom/cache/colortables-%08x.bin", paletteCRC);

		bool loaded = false;
		FILE* f = fopen(cachePath, "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			fseek(f, 0, SEEK_SET);

			if (size == (long)(sizeof(RGB32k) + sizeof(RGB256k))) {
				if (fread(&RGB32k, sizeof(RGB32k), 1, f) == 1 &&
					fread(&RGB256k, sizeof(RGB256k), 1, f) == 1) {
					loaded = true;
				}
			}
			fclose(f);
		}

		if (loaded) {
			double duration = (double)(clock() - start) / CLOCKS_PER_SEC;
			Printf("[load] colortables: cache leída/creada (%.3fs)\n", duration);
		} else {
#endif
			// create the RGB555 lookup table
			for (r = 0; r < 32; r++)
				for (g = 0; g < 32; g++)
					for (b = 0; b < 32; b++)
						RGB32k.RGB[r][g][b] = ColorMatcher.Pick ((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2));
			// create the RGB666 lookup table
			for (r = 0; r < 64; r++)
				for (g = 0; g < 64; g++)
					for (b = 0; b < 64; b++)
						RGB256k.RGB[r][g][b] = ColorMatcher.Pick ((r<<2)|(r>>4), (g<<2)|(g>>4), (b<<2)|(b>>4));

#ifdef __3DS__
			mkdir("sdmc:/3ds/gzdoom", 0777);
			mkdir("sdmc:/3ds/gzdoom/cache", 0777);
			f = fopen(cachePath, "wb");
			if (f) {
				fwrite(&RGB32k, sizeof(RGB32k), 1, f);
				fwrite(&RGB256k, sizeof(RGB256k), 1, f);
				fclose(f);
				double duration = (double)(clock() - start) / CLOCKS_PER_SEC;
				Printf("[load] colortables: cache leída/creada (%.3fs)\n", duration);
			}
		}
	}
#endif


	int x, y;

	// create the swizzled palette
	for (x = 0; x < 65; x++)
		for (y = 0; y < 256; y++)
			Col2RGB8[x][y] = (((palette[y].r*x)>>4)<<20) |
			((palette[y].g*x)>>4) |
			(((palette[y].b*x)>>4)<<10);

	// create the swizzled palette with the lsb of red and blue forced to 0
	// (for green, a 1 is okay since it never gets added into)
	for (x = 1; x < 64; x++)
	{
		Col2RGB8_LessPrecision[x] = Col2RGB8_2[x-1];
		for (y = 0; y < 256; y++)
		{
			Col2RGB8_2[x-1][y] = Col2RGB8[x][y] & 0x3feffbff;
		}
	}
	Col2RGB8_LessPrecision[0] = Col2RGB8[0];
	Col2RGB8_LessPrecision[64] = Col2RGB8[64];

	// create the inverse swizzled palette
	for (x = 0; x < 65; x++)
		for (y = 0; y < 256; y++)
		{
			Col2RGB8_Inverse[x][y] = (((((255-palette[y].r)*x)>>4)<<20) |
										  (((255-palette[y].g)*x)>>4) |
										  ((((255-palette[y].b)*x)>>4)<<10)) & 0x3feffbff;
		}
}
