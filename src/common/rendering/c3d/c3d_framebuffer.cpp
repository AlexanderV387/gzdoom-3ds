/*
** c3d_framebuffer.cpp
** The citro3d backend's frame buffer: the top screen as a citro3d render
** target, the shader program, and the engine's hardware resources.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <string.h>

#include "c3d_backend.h"
#include "c3d_internal.h"
#include "c3d_main_shbin.h"

#include "flatvertices.h"
#include "hw_skydome.h"
#include "hw_viewpointbuffer.h"
#include "hw_lightbuffer.h"
#include "hw_bonebuffer.h"
#include "hw_material.h"
#include "hw_cvars.h"
#include "hwrenderer/data/hw_vrmodes.h"
#include "v_draw.h"
#include "printf.h"
#include "engineerrors.h"
#include "platform_3ds.h"

void Draw2D(F2DDrawer *drawer, FRenderState &state);

namespace C3DRenderer
{

ShaderUniforms Uniforms;

static C3DFrameBuffer *TheFrameBuffer;
static C3DRenderState *TheRenderState;
static DVLB_s *ShaderDVLB;
static shaderProgram_s ShaderProgram;

C3DFrameBuffer *Get()
{
	return TheFrameBuffer;
}

static TArray<void *> PendingLinear;
static TArray<C3D_Tex> PendingTextures;

void DeferLinearFree(void *ptr)
{
	if (TheFrameBuffer == nullptr) linearFree(ptr);
	else PendingLinear.Push(ptr);
}

void DeferTexDelete(const C3D_Tex &tex)
{
	if (TheFrameBuffer == nullptr) { C3D_Tex t = tex; C3D_TexDelete(&t); }
	else PendingTextures.Push(tex);
}

void RunDeferredFrees()
{
	for (void *p : PendingLinear) linearFree(p);
	PendingLinear.Clear();
	for (auto &t : PendingTextures) C3D_TexDelete(&t);
	PendingTextures.Clear();
}

// The top screen: 400x240, stored turned 90 degrees as 240x400.
#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

C3DFrameBuffer::C3DFrameBuffer() : DFrameBuffer(400, 240)
{
	TheFrameBuffer = this;
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	mTop = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(mTop, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	ShaderDVLB = DVLB_ParseFile((u32 *)c3d_main_shbin, c3d_main_shbin_size);
	shaderProgramInit(&ShaderProgram);
	shaderProgramSetVsh(&ShaderProgram, &ShaderDVLB->DVLE[0]);
	C3D_BindProgram(&ShaderProgram);
	Uniforms.mvp = shaderInstanceGetUniformLocation(ShaderProgram.vertexShader, "mvp");
	Uniforms.colorscale = shaderInstanceGetUniformLocation(ShaderProgram.vertexShader, "colorscale");
	Uniforms.colormul = shaderInstanceGetUniformLocation(ShaderProgram.vertexShader, "colormul");
	Uniforms.texmat = shaderInstanceGetUniformLocation(ShaderProgram.vertexShader, "texmat");

	TheRenderState = new C3DRenderState;
}

C3DFrameBuffer::~C3DFrameBuffer()
{
	delete TheRenderState;
	TheRenderState = nullptr;
	if (mFrameStarted) C3D_FrameEnd(0);
	C3D_RenderTargetDelete(mTop);
	shaderProgramFree(&ShaderProgram);
	DVLB_Free(ShaderDVLB);
	C3D_Fini();
	TheFrameBuffer = nullptr;
}

void C3DFrameBuffer::InitializeState()
{
	mPipelineNbr = 1;
	mPipelineType = 0;
	hwcaps = RFL_NO_CLIP_PLANES;
	vendorstring = "Nintendo";

	SetViewportRects(nullptr);

	mVertexData = new FFlatVertexBuffer(GetWidth(), GetHeight(), mPipelineNbr);
	mSkyData = new FSkyVertexBuffer;
	mViewpoints = new HWViewpointBuffer(mPipelineNbr);
	mLights = new FLightBuffer(mPipelineNbr);
	mBones = new BoneBuffer(mPipelineNbr);

	Printf("C3D: backend ready, %u KB of linear memory free\n", (unsigned)(linearSpaceFree() / 1024));
}

void C3DFrameBuffer::ScreenBegin()
{
	if (mFrameStarted) return;
	#ifdef __3DS__
	if (mFrameBlocked) return;
	#endif

	// Try to start a frame. If it fails, retry with vblank waits up to ~2 seconds.
	bool ok = C3D_FrameBegin(C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK);
	#ifdef __3DS__
	if (!ok)
	{
		for (int i = 0; i < 120; i++)
		{
			gspWaitForVBlank();
			ok = C3D_FrameBegin(C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK);
			if (ok) break;
		}
		if (!ok)
		{
			mFrameBlocked = true;
			return;
		}
	}
	#endif

	if (!ok)
	{
		mFrameBlocked = true;
		return;
	}
	RunDeferredFrees();
	C3D_RenderTargetClear(mTop, C3D_CLEAR_ALL, 0x000000ff, 0);
	C3D_FrameDrawOn(mTop);
	mFrameStarted = true;
}

void C3DFrameBuffer::BeginFrame()
{
	SetViewportRects(nullptr);
	mViewpoints->Clear();
	TheRenderState->BeginFrame();
	ScreenBegin();
	#ifdef __3DS__
	if (mFrameBlocked)
	{
		return;
	}
	#endif
}

void C3DFrameBuffer::Draw2D()
{
	ScreenBegin();
	#ifdef __3DS__
	if (mFrameBlocked) return;
	#endif
	::Draw2D(twod, *TheRenderState);
}

void C3DFrameBuffer::Update()
{
	ScreenBegin();
	#ifdef __3DS__
	if (mFrameBlocked)
	{
		twod->Clear();
		TheRenderState->ResetDeviceState();
		TheRenderState->BeginFrame();
		Super::Update();
		mFrameBlocked = false;
		return;
	}
	#endif
	Draw2D();		// everything 2D still pending (menus, HUD, console, title)
	twod->Clear();
	C3D_FrameEnd(0);
	mFrameStarted = false;
	TheRenderState->ResetDeviceState();
	TheRenderState->BeginFrame();
	Super::Update();
}

IHardwareTexture *C3DFrameBuffer::CreateHardwareTexture(int numchannels)
{
	return new C3DHardwareTexture(numchannels);
}

void C3DFrameBuffer::PrecacheMaterial(FMaterial *mat, int translation)
{
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) return;
	MaterialLayerInfo *layer;
	auto base = static_cast<C3DHardwareTexture *>(mat->GetLayer(0, translation, &layer));
	if (base) base->BindOrCreate(layer->layerTexture, 0, CLAMP_NONE, translation, layer->scaleFlags & ~CTF_Indexed);
}

IVertexBuffer *C3DFrameBuffer::CreateVertexBuffer()
{
	return new C3DVertexBuffer;
}

IIndexBuffer *C3DFrameBuffer::CreateIndexBuffer()
{
	return new C3DIndexBuffer;
}

IDataBuffer *C3DFrameBuffer::CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize)
{
	return new C3DDataBuffer(bindingpoint);
}

FRenderState *C3DFrameBuffer::RenderState()
{
	return TheRenderState;
}

} // namespace C3DRenderer

//==========================================================================
//
// The video interface
//
//==========================================================================

class C3DVideo : public IVideo
{
public:
	DFrameBuffer *CreateFrameBuffer() override
	{
		return new C3DRenderer::C3DFrameBuffer;
	}
};

IVideo *C3D_CreateVideo()
{
	return new C3DVideo;
}
