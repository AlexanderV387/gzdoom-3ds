/*
** c3d_buffers.cpp
** Vertex, index and data buffers for the citro3d backend.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "c3d_backend.h"
#include "c3d_internal.h"
#include "shaderuniforms.h"
#include "engineerrors.h"

namespace C3DRenderer
{

C3DBuffer::~C3DBuffer()
{
	if (map)
	{
		if (linear) DeferLinearFree(map);
		else free(map);
	}
	map = nullptr;
}

void C3DBuffer::Allocate(size_t size)
{
	if (size <= allocated && map != nullptr)
	{
		buffersize = size;
		return;
	}
	void *mem = linear ? linearAlloc(size) : malloc(size);
	if (mem == nullptr)
	{
		I_FatalError("Out of %s memory for a %u byte buffer", linear ? "linear" : "heap", (unsigned)size);
	}
	if (map)
	{
		memcpy(mem, map, std::min(buffersize, size));
		if (linear) DeferLinearFree(map);
		else free(map);
	}
	map = mem;
	allocated = size;
	buffersize = size;
}

void C3DBuffer::SetData(size_t size, const void *data, BufferUsageType type)
{
	Allocate(size);
	if (data != nullptr) memcpy(map, data, size);
	else memset(map, 0, size);
}

void C3DBuffer::SetSubData(size_t offset, size_t size, const void *data)
{
	if (offset + size > buffersize) return;
	memcpy((uint8_t *)map + offset, data, size);
}

void *C3DBuffer::Lock(unsigned int size)
{
	if (size > buffersize) Allocate(size);
	return map;
}

void C3DBuffer::Unlock()
{
}

void C3DBuffer::Resize(size_t newsize)
{
	Allocate(newsize);
}

void C3DVertexBuffer::SetFormat(int numBindingPoints, int numAttributes, size_t stride, const FVertexBufferAttribute *attrs)
{
	mStride = stride;
	mAttributes.Clear();
	mHasColor = mHasTexCoord = false;
	for (int i = 0; i < numAttributes; i++)
	{
		// The PICA200 can read the position, the texture coordinates and the color. The rest
		// (normals, lightmap coordinates, bones, model interpolation) is not used by this backend.
		const auto &a = attrs[i];
		if (a.binding != 0) continue;
		if (a.location != VATTR_VERTEX && a.location != VATTR_TEXCOORD && a.location != VATTR_COLOR) continue;
		mAttributes.Push({ a.location, a.format, a.offset });
		if (a.location == VATTR_COLOR) mHasColor = true;
		if (a.location == VATTR_TEXCOORD) mHasTexCoord = true;
	}
	std::sort(mAttributes.begin(), mAttributes.end(), [](const C3DVertexAttribute &a, const C3DVertexAttribute &b) { return a.offset < b.offset; });
}

void C3DDataBuffer::BindRange(FRenderState *state, size_t start, size_t length)
{
	if (mBindingPoint == VIEWPOINT_BINDINGPOINT && map != nullptr)
	{
		static_cast<C3DRenderState *>(state)->mHwUniforms = (HWViewpointUniforms *)((uint8_t *)map + start);
	}
}

} // namespace C3DRenderer
