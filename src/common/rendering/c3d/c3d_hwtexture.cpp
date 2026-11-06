/*
** c3d_hwtexture.cpp
** Textures for the citro3d backend. The engine hands over BGRA images of any
** size; the PICA200 wants power-of-two sizes from 8 to 1024 pixels, stored in
** 8x8 tiles in Morton order. The image is resampled (nearest neighbour, the
** Doom look) to the power-of-two size, so texture coordinates and wrapping
** stay the same as on the desktop.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <string.h>

#include "c3d_backend.h"
#include "textures.h"
#include "hw_material.h"
#include "printf.h"

namespace C3DRenderer
{

size_t C3DHardwareTexture::AllocatedBytes = 0;

static int TexDimension(int size)
{
	int p = 8;
	while (p < size && p < 1024) p <<= 1;
	return p;
}

// Morton order inside an 8x8 tile: x bits in the even positions, y bits in the odd ones.
static inline unsigned Morton(unsigned x, unsigned y)
{
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

C3DHardwareTexture::~C3DHardwareTexture()
{
	if (mCreated)
	{
		AllocatedBytes -= mTex.size;
		C3D_TexDelete(&mTex);
	}
}

void C3DHardwareTexture::AllocateBuffer(int w, int h, int texelsize)
{
	mStagingW = w;
	mStagingH = h;
	mStaging.Resize(w * h * 4);
	memset(mStaging.Data(), 0, mStaging.Size());
}

uint8_t *C3DHardwareTexture::MapBuffer()
{
	return mStaging.Data();
}

unsigned int C3DHardwareTexture::CreateTexture(unsigned char *buffer, int w, int h, int texunit, bool mipmap, const char *name)
{
	if (w <= 0 || h <= 0) return 0;
	if (buffer == nullptr)
	{
		if (mStaging.Size() >= (unsigned)(w * h * 4)) buffer = mStaging.Data();
	}

	const int pw = TexDimension(w), ph = TexDimension(h);
	if (mCreated && (mTex.width != pw || mTex.height != ph))
	{
		AllocatedBytes -= mTex.size;
		C3D_TexDelete(&mTex);
		mCreated = false;
	}
	if (!mCreated)
	{
		if (!C3D_TexInit(&mTex, pw, ph, GPU_RGBA8))
		{
			Printf("C3D: no memory for a %dx%d texture (%s)\n", pw, ph, name ? name : "");
			return 0;
		}
		mCreated = true;
		AllocatedBytes += mTex.size;
		C3D_TexSetFilter(&mTex, GPU_NEAREST, GPU_NEAREST);
	}

	uint32_t *dst = (uint32_t *)mTex.data;
	if (buffer == nullptr)
	{
		memset(dst, 0, mTex.size);
	}
	else
	{
		const uint8_t *src = buffer;
		const unsigned tilesPerRow = pw >> 3;
		for (int y = 0; y < ph; y++)
		{
			// The PICA200's t = 0 is the first row in memory: row 0 of the image goes there, so the
			// engine's texture coordinates (v = 0 at the top) need no flip.
			const int sy = y * h / ph;
			const uint8_t *srow = src + sy * w * 4;
			const unsigned tileRow = (y >> 3) * tilesPerRow;
			for (int x = 0; x < pw; x++)
			{
				const int sx = x * w / pw;
				const uint8_t *p = srow + sx * 4;	// B, G, R, A
				const uint32_t rgba = (uint32_t(p[2]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[0]) << 8) | p[3];
				dst[((tileRow + (x >> 3)) << 6) + Morton(x & 7, y & 7)] = rgba;
			}
		}
	}
	C3D_TexFlush(&mTex);
	return 1;
}

bool C3DHardwareTexture::BindOrCreate(FTexture *tex, int texunit, int clampmode, int translation, int flags)
{
	if (!mCreated)
	{
		int w, h;
		FTextureBuffer texbuffer;
		if (!tex->isHardwareCanvas())
		{
			texbuffer = tex->CreateTexBuffer(translation, flags | CTF_ProcessData);
			w = texbuffer.mWidth;
			h = texbuffer.mHeight;
		}
		else
		{
			w = tex->GetWidth();
			h = tex->GetHeight();
		}
		if (!CreateTexture(texbuffer.mBuffer, w, h, texunit, false, "BindOrCreate")) return false;
	}
	const bool clampS = clampmode == CLAMP_X || clampmode == CLAMP_XY || clampmode == CLAMP_XY_NOMIP
		|| clampmode == CLAMP_NOFILTER_X || clampmode == CLAMP_NOFILTER_XY;
	const bool clampT = clampmode == CLAMP_Y || clampmode == CLAMP_XY || clampmode == CLAMP_XY_NOMIP
		|| clampmode == CLAMP_NOFILTER_Y || clampmode == CLAMP_NOFILTER_XY;
	C3D_TexSetWrap(&mTex, clampS ? GPU_CLAMP_TO_EDGE : GPU_REPEAT, clampT ? GPU_CLAMP_TO_EDGE : GPU_REPEAT);
	return true;
}

} // namespace C3DRenderer
