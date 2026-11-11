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
#include "c3d_diag.h"
#include "c3d_main_shbin.h"
#include "c3d_simpletri_shbin.h"

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

// Test 26: minimal shader from 3ds-examples/graphics/gpu/simple_tri.
static DVLB_s *g_SimpletriDVLB;
static shaderProgram_s g_SimpletriProgram;
static int g_SimpletriUniform_projection;
static C3D_Mtx g_SimpletriProjection;
static float *g_SimpletriVertices;

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
	if (g_SimpletriDVLB != nullptr)
	{
		shaderProgramFree(&g_SimpletriProgram);
		DVLB_Free(g_SimpletriDVLB);
		g_SimpletriDVLB = nullptr;
	}
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

	Printf("C3D: backend ready, %u KB of linear memory free (T+%lld ms)\n",
		(unsigned)(linearSpaceFree() / 1024), N3DS_ElapsedMs());
	Diag::Init();
	Diag::Line("engine shader: DVLE=%d mvp=%d colorscale=%d colormul=%d texmat=%d",
		ShaderDVLB->numDVLE, Uniforms.mvp, Uniforms.colorscale, Uniforms.colormul, Uniforms.texmat);
}

void C3DFrameBuffer::ScreenBegin()
{
	if (mFrameStarted || mFrameBlocked) return;

	// The previous frame's GPU work must have finished. A healthy GPU needs far less than a
	// frame for it; waiting up to 120 vblanks (2 s) tells a slow frame from a hung GPU. Once
	// the hang is reported, one retry per frame is enough.
	bool ok = C3D_FrameBegin(C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK);
	int retries = 0;
	const int maxRetries = Diag::HangReported() ? 1 : 120;
	while (!ok && retries < maxRetries)
	{
		gspWaitForVBlank();
		retries++;
		ok = C3D_FrameBegin(C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK);
	}
	Diag::FrameBegun(ok, retries);
	if (!ok)
	{
		mFrameBlocked = true;	// this frame draws nothing; Update() clears it for the next one
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
}

void C3DFrameBuffer::Draw2D()
{
	ScreenBegin();
	if (mFrameBlocked || Diag::Triangle) return;
	::Draw2D(twod, *TheRenderState);
}

// c3d-triangle.txt: one red triangle in clip space, with nothing of the engine's: if this shows,
// the shader, the vertex loader, the combiners and the frame output work.
static float *TriangleVertices;

// Test 28: common GPU state setup for all variants.
static void SetupCommonGPUState()
{
	C3D_SetViewport(0, 0, 240, 400);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	for (int i = 0; i < 6; i++)
	{
		C3D_TexEnv *env = C3D_GetTexEnv(i);
		C3D_TexEnvInit(env);
		if (i == 0) C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	}
}

// Test 28: set engine shader uniforms (identity matrix, neutral color scale/mul, identity texmat).
static void SetEngineUniforms(int u_mvp, int u_colorscale, int u_colormul, int u_texmat)
{
	C3D_FVec *m = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, u_mvp, 4);
	m[0] = FVec4_New(1, 0, 0, 0);
	m[1] = FVec4_New(0, 1, 0, 0);
	m[2] = FVec4_New(0, 0, 1, 0);
	m[3] = FVec4_New(0, 0, 0, 1);
	*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, u_colorscale, 1) = FVec4_New(1, 1, 1, 1);
	*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, u_colormul, 1) = FVec4_New(1, 1, 1, 1);
	C3D_FVec *t = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, u_texmat, 2);
	t[0] = FVec4_New(1, 0, 0, 0);
	t[1] = FVec4_New(0, 1, 0, 0);
}

static void DrawTestTriangle()
{
	if (Diag::MinimalShader)
	{
		// --- Minimal shader path (simple_tri) ---
		if (g_SimpletriDVLB == nullptr)
		{
			g_SimpletriDVLB = DVLB_ParseFile((u32 *)c3d_simpletri_shbin, c3d_simpletri_shbin_size);
			shaderProgramInit(&g_SimpletriProgram);
			shaderProgramSetVsh(&g_SimpletriProgram, &g_SimpletriDVLB->DVLE[0]);
			C3D_BindProgram(&g_SimpletriProgram);
			g_SimpletriUniform_projection = shaderInstanceGetUniformLocation(g_SimpletriProgram.vertexShader, "projection");
			Mtx_OrthoTilt(&g_SimpletriProjection, 0, 400, 0, 240, 0, 1, true);
			Diag::Line("simpletri shader: DVLE=%d projection=%d",
				g_SimpletriDVLB->numDVLE, g_SimpletriUniform_projection);

			// Vertices: {x, y, z} for simple_tri (screen coords)
			static const float v[] = {
				200.f, 200.f, 0.5f,
				100.f,  40.f, 0.5f,
				300.f,  40.f, 0.5f,
			};
			g_SimpletriVertices = (float *)linearAlloc(sizeof(v));
			memcpy(g_SimpletriVertices, v, sizeof(v));
			GSPGPU_FlushDataCache(g_SimpletriVertices, sizeof(v));
		}

		C3D_BindProgram(&g_SimpletriProgram);
		C3D_SetViewport(0, 0, 240, 400);
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		for (int i = 0; i < 6; i++)
		{
			C3D_TexEnv *env = C3D_GetTexEnv(i);
			C3D_TexEnvInit(env);
			if (i == 0) C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		}

		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_SimpletriUniform_projection, &g_SimpletriProjection);

		C3D_AttrInfo *ai = C3D_GetAttrInfo();
		AttrInfo_Init(ai);
		AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);			// v0: position
		C3D_FixedAttribSet(AttrInfo_AddFixed(ai, 1), 1, 0, 0, 1);	// v1: red
		C3D_BufInfo *bi = C3D_GetBufInfo();
		BufInfo_Init(bi);
		BufInfo_Add(bi, g_SimpletriVertices, 3 * sizeof(float), 1, 0x0);
		C3D_DrawArrays(GPU_TRIANGLES, 0, 3);
		Diag::Draw(false, true);
		return;
	}

	// --- Engine shader path (unchanged) ---
	if (TriangleVertices == nullptr)
	{
		static const float v[9] = { -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.f, 0.5f, -0.5f };
		TriangleVertices = (float *)linearAlloc(sizeof(v));
		memcpy(TriangleVertices, v, sizeof(v));
		GSPGPU_FlushDataCache(TriangleVertices, sizeof(v));
	}
	C3D_SetViewport(0, 0, 240, 400);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	for (int i = 0; i < 6; i++)
	{
		C3D_TexEnv *env = C3D_GetTexEnv(i);
		C3D_TexEnvInit(env);
		if (i == 0) C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	}

	C3D_FVec *m = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.mvp, 4);
	m[0] = FVec4_New(1, 0, 0, 0);
	m[1] = FVec4_New(0, 1, 0, 0);
	m[2] = FVec4_New(0, 0, 1, 0);
	m[3] = FVec4_New(0, 0, 0, 1);
	*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colorscale, 1) = FVec4_New(1, 1, 1, 1);
	*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colormul, 1) = FVec4_New(1, 1, 1, 1);
	C3D_FVec *t = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.texmat, 2);
	t[0] = FVec4_New(1, 0, 0, 0);
	t[1] = FVec4_New(0, 1, 0, 0);

	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);			// v0: position
	C3D_FixedAttribSet(AttrInfo_AddFixed(ai, 1), 0, 0, 0, 0);	// v1: texture coordinates
	C3D_FixedAttribSet(AttrInfo_AddFixed(ai, 2), 1, 0, 0, 1);	// v2: red
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, TriangleVertices, 3 * sizeof(float), 1, 0x0);
	C3D_DrawArrays(GPU_TRIANGLES, 0, 3);
	Diag::Draw(false, true);
}

void C3DFrameBuffer::Update()
{
	ScreenBegin();
	if (mFrameBlocked)
	{
		// No frame could be started: drop this frame's 2D and try again on the next one.
		twod->Clear();
		TheRenderState->ResetDeviceState();
		TheRenderState->BeginFrame();
		Diag::FrameDone(false);
		Super::Update();
		mFrameBlocked = false;
		return;
	}
	if (Diag::Triangle) DrawTestTriangle();
	else if (Diag::TestFlip)
	{
		// Test 31: two coloured quads via the engine 2D path, no textures.
		// Red 60x60 at top-left (0,0), green 60x60 at bottom-left (0, height-60).
		int h = TheFrameBuffer->GetHeight();
		static bool first = true;
		if (first)
		{
			Diag::Line("testflip: height=%d red=(0,0,60,60) green=(0,%d,60,60)", h, h - 60);
			first = false;
		}
		twod->AddColorOnlyQuad(0, 0, 60, 60, 0xffff0000);
		twod->AddColorOnlyQuad(0, h - 60, 60, 60, 0xff00ff00);
		::Draw2D(twod, *TheRenderState);
	}
	else
	{
		Draw2D();	// everything 2D still pending (menus, HUD, console, title)
	}
	twod->Clear();
	C3D_FrameEnd(0);
	mFrameStarted = false;
	Diag::FrameDone(true);
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
