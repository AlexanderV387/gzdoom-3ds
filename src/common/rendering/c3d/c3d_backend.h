/*
** c3d_backend.h
** The Nintendo 3DS video backend: UZDoom's hardware renderer drawn by the
** PICA200 through citro3d. Everything the engine draws (2D and the 3D world)
** goes through FRenderState, as with the OpenGL, GLES and Vulkan backends;
** this one emulates their shaders with the PICA200's fixed-function texture
** combiners (TEV), its alpha test, blending, depth, stencil and fog.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "buffers.h"
#include "hw_ihwtexture.h"
#include "hw_renderstate.h"
#include "hw_viewpointuniforms.h"
#include "v_video.h"
#include "i_video.h"
#include "matrix.h"

class FTexture;

namespace C3DRenderer
{

//==========================================================================
//
// Buffers. Vertex buffers live in linear memory, where the PICA200 reads
// them directly; index buffers and data (uniform) buffers stay in ordinary
// memory: indices are narrowed to 16 bits when drawn and the uniforms are
// read by the CPU.
//
//==========================================================================

class C3DBuffer : virtual public IBuffer
{
protected:
	bool linear;
	size_t allocated = 0;

public:
	C3DBuffer(bool inLinearMemory) : linear(inLinearMemory) {}
	~C3DBuffer();

	void SetData(size_t size, const void *data, BufferUsageType type) override;
	void SetSubData(size_t offset, size_t size, const void *data) override;
	void *Lock(unsigned int size) override;
	void Unlock() override;
	void Resize(size_t newsize) override;
	void Map() override {}
	void Unmap() override {}

	void Allocate(size_t size);
};

struct C3DVertexAttribute
{
	int location;	// VATTR_*
	int format;		// VFmt_*
	int offset;
};

class C3DVertexBuffer : public IVertexBuffer, public C3DBuffer
{
public:
	C3DVertexBuffer() : C3DBuffer(true) {}
	void SetFormat(int numBindingPoints, int numAttributes, size_t stride, const FVertexBufferAttribute *attrs) override;

	size_t mStride = 0;
	TArray<C3DVertexAttribute> mAttributes;	// binding point 0 only, sorted by offset
	bool mHasColor = false;
	bool mHasTexCoord = false;
};

class C3DIndexBuffer : public IIndexBuffer, public C3DBuffer
{
public:
	C3DIndexBuffer() : C3DBuffer(false) {}
};

class C3DDataBuffer : public IDataBuffer, public C3DBuffer
{
	int mBindingPoint;
public:
	C3DDataBuffer(int bindingpoint) : C3DBuffer(false), mBindingPoint(bindingpoint) {}
	void BindRange(FRenderState *state, size_t start, size_t length) override;
};

//==========================================================================
//
// Textures: converted to the PICA200's tiled RGBA8 format, resampled to
// power-of-two sizes (8 to 1024) so that wrapping keeps working.
//
//==========================================================================

class C3DHardwareTexture : public IHardwareTexture
{
	C3D_Tex mTex = {};
	bool mCreated = false;
	int mChannels;
	TArray<uint8_t> mStaging;	// AllocateBuffer / MapBuffer
	int mStagingW = 0, mStagingH = 0;

public:
	C3DHardwareTexture(int numchannels) : mChannels(numchannels) {}
	~C3DHardwareTexture();

	void AllocateBuffer(int w, int h, int texelsize) override;
	uint8_t *MapBuffer() override;
	unsigned int CreateTexture(unsigned char *buffer, int w, int h, int texunit, bool mipmap, const char *name) override;

	bool BindOrCreate(FTexture *tex, int texunit, int clampmode, int translation, int flags);
	C3D_Tex *GetTex() { return mCreated ? &mTex : nullptr; }

	static size_t AllocatedBytes;
};

//==========================================================================
//
// Render state
//
//==========================================================================

class C3DRenderState : public FRenderState
{
public:
	HWViewpointUniforms *mHwUniforms = nullptr;

	C3DRenderState() { Reset(); }

	void ClearScreen() override;
	void Draw(int dt, int index, int count, bool apply = true) override;
	void DrawIndexed(int dt, int index, int count, bool apply = true) override;

	bool SetDepthClamp(bool on) override;
	void SetDepthMask(bool on) override;
	void SetDepthFunc(int func) override;
	void SetDepthRange(float min, float max) override;
	using FRenderState::SetColorMask;
	void SetColorMask(bool r, bool g, bool b, bool a) override;
	void SetStencil(int offs, int op, int flags = -1) override;
	void SetCulling(int mode) override;
	void EnableClipDistance(int num, bool state) override {}
	void Clear(int targets) override;
	void EnableStencil(bool on) override;
	void SetScissor(int x, int y, int w, int h) override;
	void SetViewport(int x, int y, int w, int h) override;
	void EnableDepthTest(bool on) override;
	void EnableMultisampling(bool on) override {}
	void EnableLineSmooth(bool on) override {}
	void EnableDrawBuffers(int count, bool apply = false) override {}

	void BeginFrame();
	void ResetDeviceState();

private:
	bool Apply(int dt);
	void ApplyMaterial();
	void ApplyBlendMode();
	void ApplyCombiners(bool textured, int texMode);
	void ApplyMatrices();
	bool SetupAttributes(int baseVertex);
	void DescribeDraw(const char *kind, int dt, int first, int count, const void *vertices, const void *indices);

	// Device state that FRenderState does not hold.
	bool mDepthTest = false;
	bool mDepthMask = true;
	int mDepthFunc = DF_Less;
	bool mStencilEnabled = false;
	int mStencilRef = 0, mStencilOp = SOP_Keep, mStencilFlags = -1;
	int mCulling = 0;
	uint8_t mColorWrite = 0xf;

	// Change tracking.
	C3DHardwareTexture *mBoundTexture = nullptr;
	FMaterial *mLastMaterial = nullptr;
	int mLastClamp = -1, mLastTranslation = -1;
	int mTexClamp = 0;
};

//==========================================================================
//
// Frame buffer: the top screen (400x240) as a citro3d render target
//
//==========================================================================

class C3DFrameBuffer : public DFrameBuffer
{
	typedef DFrameBuffer Super;

public:
	C3DFrameBuffer();
	~C3DFrameBuffer();

	void InitializeState() override;
	void Update() override;
	bool IsFullscreen() override { return true; }
	int GetClientWidth() override { return 400; }
	int GetClientHeight() override { return 240; }
	const char *DeviceName() const override { return "PICA200 (citro3d)"; }
	int Backend() override { return 3; }

	IHardwareTexture *CreateHardwareTexture(int numchannels) override;
	void PrecacheMaterial(FMaterial *mat, int translation) override;
	IVertexBuffer *CreateVertexBuffer() override;
	IIndexBuffer *CreateIndexBuffer() override;
	IDataBuffer *CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize) override;
	FRenderState *RenderState() override;
	void BeginFrame() override;
	void Draw2D() override;
	void SetVSync(bool vsync) override {}
	void WaitForCommands(bool finish) override {}
	bool RenderTextureIsFlipped() const override { return false; }

	void ScreenBegin();	// starts the citro3d frame if needed
	C3D_RenderTarget *Target() { return mTop; }
	bool IsFrameBlocked() const { return mFrameBlocked; }	// no citro3d frame this time

private:
	C3D_RenderTarget *mTop = nullptr;
	bool mFrameStarted = false;
	bool mFrameBlocked = false;
};

C3DFrameBuffer *Get();

} // namespace C3DRenderer

IVideo *C3D_CreateVideo();
