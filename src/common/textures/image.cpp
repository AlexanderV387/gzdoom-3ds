/*
** image.cpp
**
** The base texture class
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2016 Marisa Heit
** Copyright 2005-2018 Christoph Oelckers
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

#include "bitmap.h"
#include "image.h"
#include "filesystem.h"
#include "files.h"
#include "cmdlib.h"
#include "palettecontainer.h"
#include "printf.h"
#include "common/utility/i_time.h"

namespace FileSys {
	void GetFopenStats(uint64_t &n, uint64_t &ms);
	bool IsFileSystemMainThread();
}

FMemArena ImageArena(32768);
TArray<FImageSource *>FImageSource::ImageForLump;
int FImageSource::NextID;
static PrecacheInfo precacheInfo;

struct PrecacheDataPaletted
{
	PalettedPixels Pixels;
	int RefCount;
	int ImageID;
	int Frame;
};

struct PrecacheDataRgba
{
	FBitmap Pixels;
	int TransInfo;
	int RefCount;
	int ImageID;
	int Frame;
};

// TMap doesn't handle this kind of data well.  std::map neither. The linear search is still faster, even for a few 100 entries because it doesn't have to access the heap as often..
TArray<PrecacheDataPaletted> precacheDataPaletted;
TArray<PrecacheDataRgba> precacheDataRgba;

static uint64_t g_ImageStats_OpenCalls = 0;
static uint64_t g_ImageStats_OpenTimeMs = 0;
static uint64_t g_ImageStats_TryCreateTimeMs = 0;
static uint64_t g_ImageStats_Bytes = 0;
static uint32_t g_ImageStats_TryCreateSuccess[15] = {0};

void PrintImageStats()
{
	Printf("ImageStats: calls=%llu, open_ms=%llu, trycreate_ms=%llu, bytes=%llu\n", 
	       g_ImageStats_OpenCalls, g_ImageStats_OpenTimeMs, g_ImageStats_TryCreateTimeMs, g_ImageStats_Bytes);
	for (int i = 0; i < 15; i++) {
		if (g_ImageStats_TryCreateSuccess[i] > 0) {
			Printf("  TryCreate[%d] success=%u\n", i, g_ImageStats_TryCreateSuccess[i]);
		}
	}
	Printf("[load] mainThread=%d\n", FileSys::IsFileSystemMainThread());
	uint64_t fn = 0, fms = 0;
	FileSys::GetFopenStats(fn, fms);
	Printf("[load] fopen: n=%llu ms=%llu\n", (unsigned long long)fn, (unsigned long long)fms);

	g_ImageStats_OpenCalls = 0;
	g_ImageStats_OpenTimeMs = 0;
	g_ImageStats_TryCreateTimeMs = 0;
	g_ImageStats_Bytes = 0;
	for (int i = 0; i < 15; i++) g_ImageStats_TryCreateSuccess[i] = 0;
}

#ifdef __3DS__
//===========================================================================
//
// 3DS image cache. Detecting the format of every graphic in an SD card WAD
// reads ~12 MB from the card at startup (doom2.wad: ~4.4 s). The result of
// that detection (patch header values, flat, or "not an image") is saved per
// WAD in sdmc:/3ds/gzdoom/cache/img-XXXXXXXX.bin, so later boots create the
// image sources without reading the lumps. Only patches and flats are cached.
// The file is invalidated by the WAD's size, date and lump count.
//
//===========================================================================

#include <3ds.h>
#include <sys/stat.h>
#include <strings.h>
#include <vector>
#include <algorithm>
#include "m_crc32.h"

FImageSource *PatchImage_CreateCached(int lumpnum, int width, int height, int leftoffset, int topoffset, bool isalpha);
FImageSource *FlatImage_TryCreate(FileReader &, int lumpnum);

enum : uint8_t { IC_UNKNOWN = 0, IC_NONE = 1, IC_NONE_FLAT = 2, IC_PATCH = 3, IC_FLAT = 4 };

#pragma pack(push, 1)
struct ImgCacheEntry { uint8_t kind; uint8_t isalpha; int16_t w, h, lo, to; };
struct ImgCacheHeader { uint32_t magic, version, filesize, mtime, numlumps, entrysize; };
#pragma pack(pop)

static const uint32_t IMGCACHE_MAGIC = 0x43494433;	// "3DIC"
static const uint32_t IMGCACHE_VERSION = 1;

struct ImgCacheWad
{
	bool usable = false;	// a .wad on the SD card
	bool dirty = false;		// entries changed since the file was read
	int firstlump = 0;
	ImgCacheHeader hdr = {};
	char path[128] = {};
	std::vector<ImgCacheEntry> entries;
};

static std::vector<ImgCacheWad *> g_ImgCache;	// indexed by wad number
static uint64_t g_ImageStats_CacheHits = 0;

static ImgCacheWad *ImgCacheFor(int lumpnum)
{
	int wadnum = fileSystem.GetFileContainer(lumpnum);
	if (wadnum < 0) return nullptr;
	if (g_ImgCache.size() <= (size_t)wadnum) g_ImgCache.resize(wadnum + 1, nullptr);
	if (g_ImgCache[wadnum] != nullptr) return g_ImgCache[wadnum]->usable ? g_ImgCache[wadnum] : nullptr;

	ImgCacheWad *c = new ImgCacheWad;
	g_ImgCache[wadnum] = c;

	const char *name = fileSystem.GetResourceFileFullName(wadnum);
	size_t len = name ? strlen(name) : 0;
	struct stat st;
	if (len < 9 || strncmp(name, "sdmc:", 5) != 0 || strcasecmp(name + len - 4, ".wad") != 0 || stat(name, &st) != 0)
		return nullptr;

	c->usable = true;
	c->firstlump = fileSystem.GetFirstEntry(wadnum);
	int numlumps = fileSystem.GetLastEntry(wadnum) - c->firstlump + 1;
	c->hdr.magic = IMGCACHE_MAGIC;
	c->hdr.version = IMGCACHE_VERSION;
	c->hdr.filesize = (uint32_t)st.st_size;
	c->hdr.mtime = (uint32_t)st.st_mtime;
	c->hdr.numlumps = (uint32_t)numlumps;
	c->hdr.entrysize = (uint32_t)sizeof(ImgCacheEntry);
	snprintf(c->path, sizeof(c->path), "sdmc:/3ds/gzdoom/cache/img-%08x.bin",
		(unsigned)(CalcCRC32((const uint8_t *)name, (unsigned)len) ^ c->hdr.filesize));
	c->entries.assign(numlumps, ImgCacheEntry{});

	uint64_t start = I_msTime();
	FILE *f = fopen(c->path, "rb");
	if (f != nullptr)
	{
		ImgCacheHeader h;
		bool ok = fread(&h, sizeof(h), 1, f) == 1 && memcmp(&h, &c->hdr, sizeof(h)) == 0 &&
			fread(c->entries.data(), sizeof(ImgCacheEntry), numlumps, f) == (size_t)numlumps;
		fclose(f);
		if (!ok) c->entries.assign(numlumps, ImgCacheEntry{});
		Printf("[load] imgcache: %s %s (%llums)\n", c->path, ok ? "leída" : "no válida", (unsigned long long)(I_msTime() - start));
	}
	else
	{
		Printf("[load] imgcache: %s no existe, se creará\n", c->path);
	}
	return c;
}

struct ImgCacheWriteData
{
	char path[128];
	ImgCacheHeader hdr;
	std::vector<ImgCacheEntry> entries;
};

static void ImgCacheWriteThread(void *arg)
{
	ImgCacheWriteData *data = (ImgCacheWriteData *)arg;
	FILE *f = fopen(data->path, "wb");
	if (f != nullptr)
	{
		bool ok = fwrite(&data->hdr, sizeof(data->hdr), 1, f) == 1 &&
			fwrite(data->entries.data(), sizeof(ImgCacheEntry), data->entries.size(), f) == data->entries.size();
		fclose(f);
		if (!ok) remove(data->path);
	}
	delete data;
}

// Called once after all WADs' textures were added (texturemanager.cpp).
void ImageCache3DS_Save()
{
	for (ImgCacheWad *c : g_ImgCache)
	{
		if (c == nullptr || !c->usable || !c->dirty) continue;
		mkdir("sdmc:/3ds/gzdoom", 0777);
		mkdir("sdmc:/3ds/gzdoom/cache", 0777);
		ImgCacheWriteData *data = new ImgCacheWriteData;
		memcpy(data->path, c->path, sizeof(data->path));
		data->hdr = c->hdr;
		data->entries = c->entries;
		int priority = 0x30;	// same as the colour table cache writer (v_colortables.cpp)
#ifdef CUR_THREAD_HANDLE
		svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
#endif
		priority = std::clamp(priority + 4, 0x18, 0x3F);
		if (threadCreate(ImgCacheWriteThread, data, 32768, priority, 0, true) == nullptr) delete data;
		c->dirty = false;
		Printf("[load] imgcache: %s se guarda en segundo plano\n", c->path);
	}
	Printf("[load] imgcache: aciertos=%llu\n", (unsigned long long)g_ImageStats_CacheHits);
	g_ImageStats_CacheHits = 0;
}
#endif


//===========================================================================
//
// the default just returns an empty texture.
//
//===========================================================================

PalettedPixels FImageSource::CreatePalettedPixels(int conversion, int frame)
{
	PalettedPixels Pixels(Width * Height);
	memset(Pixels.Data(), 0, Width * Height);
	return Pixels;
}

PalettedPixels FImageSource::GetCachedPalettedPixels(int conversion, int frame)
{
	PalettedPixels ret;

	auto imageID = ImageID;

	// Do we have this image in the cache?
	unsigned index = conversion != normal? UINT_MAX : precacheDataPaletted.FindEx([=](PrecacheDataPaletted &entry) { return entry.ImageID == imageID && entry.Frame == frame; });
	if (index < precacheDataPaletted.Size())
	{
		auto cache = &precacheDataPaletted[index];

		if (cache->RefCount > 1)
		{
			//Printf("returning reference to %s, refcount = %d\n", name.GetChars(), cache->RefCount);
			ret.Pixels.Set(cache->Pixels.Data(), cache->Pixels.Size());
			cache->RefCount--;
		}
		else if (cache->Pixels.Size() > 0)
		{
			//Printf("returning contents of %s, refcount = %d\n", name.GetChars(), cache->RefCount);
			ret = std::move(cache->Pixels);
			precacheDataPaletted.Delete(index);
		}
		else
		{
			//Printf("something bad happened for %s, refcount = %d\n", name.GetChars(), cache->RefCount);
		}
	}
	else
	{
		// The image wasn't cached. Now there's two possibilities:
		auto info = precacheInfo.CheckKey(ImageID);
		if (!info || info->second <= 1 || conversion != normal)
		{
			// This is either the only copy needed or some access outside the caching block. In these cases create a new one and directly return it.
			//Printf("returning fresh copy of %s\n", name.GetChars());
			return CreatePalettedPixels(conversion, frame);
		}
		else
		{
			//Printf("creating cached entry for %s, refcount = %d\n", name.GetChars(), info->second);
			// This is the first time it gets accessed and needs to be placed in the cache.
			PrecacheDataPaletted *pdp = &precacheDataPaletted[precacheDataPaletted.Reserve(1)];

			pdp->ImageID = imageID;
			pdp->RefCount = info->second - 1;
			info->second = 0;
			pdp->Pixels = CreatePalettedPixels(normal, frame);
			ret.Pixels.Set(pdp->Pixels.Data(), pdp->Pixels.Size());
		}
	}
	return ret;
}

TArray<uint8_t> FImageSource::GetPalettedPixels(int conversion, int frame)
{
	auto pix = GetCachedPalettedPixels(conversion, frame);
	if (pix.ownsPixels())
	{
		// return the pixel store of the returned data directly if this was the last reference.
		auto array = std::move(pix.PixelStore);
		return array;
	}
	else
	{
		// If there are pending references, make a copy.
		TArray<uint8_t> array(pix.Pixels.Size(), true);
		memcpy(array.Data(), pix.Pixels.Data(), array.Size());
		return array;
	}
}



//===========================================================================
//
// FImageSource::CopyPixels
//
// this is the generic case that can handle
// any properly implemented texture for software rendering.
// Its drawback is that it is limited to the base palette which is
// why all classes that handle different palettes should subclass this
// method
//
//===========================================================================

int FImageSource::CopyPixels(FBitmap *bmp, int conversion, int frame)
{
	if (conversion == luminance) conversion = normal;	// luminance images have no use as an RGB source.
	PalEntry *palette = GPalette.BaseColors;

	auto ppix = CreatePalettedPixels(conversion, frame);
	bmp->CopyPixelData(0, 0, ppix.Data(), Width, Height, Height, 1, 0, palette, nullptr);
	return 0;
}

int FImageSource::CopyTranslatedPixels(FBitmap *bmp, const PalEntry *remap, int frame)
{
	auto ppix = CreatePalettedPixels(normal, frame);
	bmp->CopyPixelData(0, 0, ppix.Data(), Width, Height, Height, 1, 0, remap, nullptr);
	return 0;
}

//==========================================================================
//
//
//
//==========================================================================

FBitmap FImageSource::GetCachedBitmap(const PalEntry *remap, int conversion, int *ptrans, int frame)
{
	FBitmap ret;

	int trans = -1;
	auto imageID = ImageID;

	if (NumOfFrames == 1 && frame == 1)
	{
		frame = 0;
	}

	if (remap != nullptr)
	{
		// Remapped images are never run through the cache because they would complicate matters too much for very little gain.
		// Translated images are normally sprites which normally just consist of a single image and use no composition.
		// Additionally, since translation requires the base palette, the really time consuming stuff will never be subjected to it.
		ret.Create(Width, Height);
		trans = CopyTranslatedPixels(&ret, remap, frame);
	}
	else
	{
		if (conversion == luminance) conversion = normal;	// luminance has no meaning for true color.
		// Do we have this image in the cache?
		unsigned index = conversion != normal? UINT_MAX : precacheDataRgba.FindEx([=](PrecacheDataRgba &entry) { return entry.ImageID == imageID && entry.Frame == frame; });
		if (index < precacheDataRgba.Size())
		{
			auto cache = &precacheDataRgba[index];

			trans = cache->TransInfo;
			if (cache->RefCount > 1)
			{
				//Printf("returning reference to %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret.Copy(cache->Pixels, false);
				cache->RefCount--;
			}
			else if (cache->Pixels.GetPixels())
			{
				//Printf("returning contents of %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret = std::move(cache->Pixels);
				precacheDataRgba.Delete(index);
			}
			else
			{
				// This should never happen if the function is implemented correctly
				//Printf("something bad happened for %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret.Create(Width, Height);
				trans = CopyPixels(&ret, normal, frame);
			}
		}
		else
		{
			// The image wasn't cached. Now there's two possibilities:
			auto info = precacheInfo.CheckKey(ImageID);
			if (!info || info->first <= 1 || conversion != normal)
			{
				// This is either the only copy needed or some access outside the caching block. In these cases create a new one and directly return it.
				//Printf("returning fresh copy of %s\n", name.GetChars());
				ret.Create(Width, Height);
				trans = CopyPixels(&ret, conversion, frame);
			}
			else
			{
				//Printf("creating cached entry for %s, refcount = %d\n", name.GetChars(), info->first);
				// This is the first time it gets accessed and needs to be placed in the cache.
				PrecacheDataRgba *pdr = &precacheDataRgba[precacheDataRgba.Reserve(1)];

				pdr->ImageID = imageID;
				pdr->Frame = frame;
				pdr->RefCount = info->first - 1;
				info->first = 0;
				pdr->Pixels.Create(Width, Height);
				trans = pdr->TransInfo = CopyPixels(&pdr->Pixels, normal, frame);
				ret.Copy(pdr->Pixels, false);
			}
		}
	}
	if (ptrans) *ptrans = trans;
	return ret;
}

//==========================================================================
//
//
//
//==========================================================================

void FImageSource::CollectForPrecache(PrecacheInfo &info, bool requiretruecolor)
{
	auto val = info.CheckKey(ImageID);
	bool tc = requiretruecolor;
	if (val)
	{
		val->first += tc;
		val->second += !tc;
	}
	else
	{
		auto pair = std::make_pair(tc, !tc);
		info.Insert(ImageID, pair);
	}
}

void FImageSource::BeginPrecaching()
{
	precacheInfo.Clear();
}

void FImageSource::EndPrecaching()
{
	precacheDataPaletted.Clear();
	precacheDataRgba.Clear();
}

void FImageSource::RegisterForPrecache(FImageSource *img, bool requiretruecolor)
{
	if (img)
		img->CollectForPrecache(precacheInfo, requiretruecolor);
}

//==========================================================================
//
//
//
//==========================================================================

typedef FImageSource * (*CreateFunc)(FileReader & file, int lumpnum);

struct TexCreateInfo
{
	CreateFunc TryCreate;
	bool checkflat;
};

FImageSource *IMGZImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PNGImage_TryCreate(FileReader &, int lumpnum);
FImageSource *DDSImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PCXImage_TryCreate(FileReader &, int lumpnum);
FImageSource *TGAImage_TryCreate(FileReader &, int lumpnum);
FImageSource *StbImage_TryCreate(FileReader &, int lumpnum);
FImageSource *QOIImage_TryCreate(FileReader &, int lumpnum);
FImageSource *WebPImage_TryCreate(FileReader &, int lumpnum);
FImageSource *AnmImage_TryCreate(FileReader &, int lumpnum);
FImageSource *RawPageImage_TryCreate(FileReader &, int lumpnum);
FImageSource *FlatImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PatchImage_TryCreate(FileReader &, int lumpnum);
FImageSource *EmptyImage_TryCreate(FileReader &, int lumpnum);
FImageSource *AutomapImage_TryCreate(FileReader &, int lumpnum);
FImageSource *StartupPageImage_TryCreate(FileReader &, int lumpnum);


// Examines the lump contents to decide what type of texture to create,
// and creates the texture.
FImageSource * FImageSource::GetImage(int lumpnum, bool isflat)
{
	static TexCreateInfo CreateInfo[] = {
		{ IMGZImage_TryCreate,			false },
		{ PNGImage_TryCreate,			false },
		{ DDSImage_TryCreate,			false },
		{ PCXImage_TryCreate,			false },
		{ StbImage_TryCreate,			false },
		{ QOIImage_TryCreate, 			false },
		{ WebPImage_TryCreate,			false },
		{ TGAImage_TryCreate,			false },
		{ AnmImage_TryCreate,			false },
		{ StartupPageImage_TryCreate,	false },
		{ RawPageImage_TryCreate,		false },
		{ FlatImage_TryCreate,			true },	// flat detection is not reliable, so only consider this for real flats.
		{ PatchImage_TryCreate,			false },
		{ EmptyImage_TryCreate,			false },
		{ AutomapImage_TryCreate,		false },
	};

	if (lumpnum == -1) return nullptr;

	unsigned size = ImageForLump.Size();
	if (size <= (unsigned)lumpnum)
	{
		// Hires textures can be added dynamically to the end of the lump array, so this must be checked each time.
		ImageForLump.Resize(lumpnum + 1);
		for (; size < ImageForLump.Size(); size++) ImageForLump[size] = nullptr;
	}
	// An image for this lump already exists. We do not need another one.
	if (ImageForLump[lumpnum] != nullptr) return ImageForLump[lumpnum];

#ifdef __3DS__
	// A cached detection result makes reading the lump unnecessary (see ImgCacheFor).
	ImgCacheWad *icw = ImgCacheFor(lumpnum);
	ImgCacheEntry *ice = nullptr;
	if (icw != nullptr && lumpnum >= icw->firstlump && (size_t)(lumpnum - icw->firstlump) < icw->entries.size())
		ice = &icw->entries[lumpnum - icw->firstlump];
	if (ice != nullptr)
	{
		if (ice->kind == IC_PATCH)
		{
			g_ImageStats_CacheHits++;
			ImageForLump[lumpnum] = PatchImage_CreateCached(lumpnum, ice->w, ice->h, ice->lo, ice->to, ice->isalpha != 0);
			return ImageForLump[lumpnum];
		}
		if (ice->kind == IC_FLAT && isflat)
		{
			g_ImageStats_CacheHits++;
			FileReader unused;
			ImageForLump[lumpnum] = FlatImage_TryCreate(unused, lumpnum);
			return ImageForLump[lumpnum];
		}
		if (ice->kind == (isflat ? IC_NONE_FLAT : IC_NONE))
		{
			g_ImageStats_CacheHits++;
			return nullptr;
		}
	}
#endif

	g_ImageStats_OpenCalls++;
	uint64_t openStart = I_msTime();
	auto data = fileSystem.OpenFileReader(lumpnum);
	g_ImageStats_OpenTimeMs += (I_msTime() - openStart);

	if (!data.isOpen())
		return nullptr;

	g_ImageStats_Bytes += data.GetLength();

	uint64_t tryStart = I_msTime();
	for (size_t i = 0; i < countof(CreateInfo); i++)
	{
		if (!CreateInfo[i].checkflat || isflat)
		{
			auto image = CreateInfo[i].TryCreate(data, lumpnum);
			if (image != nullptr)
			{
				g_ImageStats_TryCreateSuccess[i]++;
				ImageForLump[lumpnum] = image;
#ifdef __3DS__
				// Only flats and Doom patches are cached: indexes of FlatImage_TryCreate and
				// PatchImage_TryCreate in CreateInfo above.
				if (ice != nullptr && (i == 11 || i == 12))
				{
					auto ofs = image->GetOffsets();
					ice->kind = (i == 12) ? IC_PATCH : IC_FLAT;
					ice->isalpha = image->UseGamePalette() ? 0 : 1;
					ice->w = (int16_t)image->GetWidth();
					ice->h = (int16_t)image->GetHeight();
					ice->lo = (int16_t)ofs.first;
					ice->to = (int16_t)ofs.second;
					icw->dirty = true;
				}
#endif
				g_ImageStats_TryCreateTimeMs += (I_msTime() - tryStart);
				return image;
			}
		}
	}
	g_ImageStats_TryCreateTimeMs += (I_msTime() - tryStart);
#ifdef __3DS__
	if (ice != nullptr)
	{
		ice->kind = isflat ? IC_NONE_FLAT : IC_NONE;
		icw->dirty = true;
	}
#endif

	return nullptr;
}
