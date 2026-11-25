/*
** c3d_renderstate.cpp
** FRenderState for the citro3d backend: turns the engine's render state into
** PICA200 state (combiners, blending, alpha test, depth, stencil) and draws.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <string.h>
#include <algorithm>

#include "c3d_backend.h"
#include "c3d_internal.h"
#include "c3d_diag.h"
#include "hw_material.h"
#include "texturemanager.h"
#include "printf.h"
#include "platform_3ds.h"

#ifdef __3DS__
EXTERN_CVAR(Float, c3d_depthbias)
EXTERN_CVAR(Int, c3d_wipeorient)
#endif

namespace C3DRenderer
{

// Index ring: the engine's 32-bit indices are narrowed to the PICA200's 16 bits here.
static uint16_t *IndexRing = nullptr;
static size_t IndexRingPos = 0;
static const size_t IndexRingSize = 512 * 1024;	// indices per frame

static bool WarnedLines = false;
static bool WarnedViewport = false;
static bool WarnedCmdBuf = false;

// The PICA200 takes unsigned viewport and scissor coordinates: a rectangle that leaves the
// 240x400 frame buffer wraps around instead of being clipped (test 44: a 640x400 viewport
// drew the menu enlarged and repeated on the console). Keep it inside.
static bool ClampToFrameBuffer(int &left, int &top, int &right, int &bottom)
{
	const int l = left, t = top, r = right, b = bottom;
	left = std::clamp(left, 0, 240);
	right = std::clamp(right, left, 240);
	top = std::clamp(top, 0, 400);
	bottom = std::clamp(bottom, top, 400);
	return l != left || t != top || r != right || b != bottom;
}

// citro3d's command buffer is one block per frame; libctru stops the program (svcBreak) when
// it overflows (test 36: opening OPTIONS). A draw needs room for its state as well.
static bool CommandBufferHasRoom()
{
	u32 size = 0, offset = 0;
	GPUCMD_GetBuffer(nullptr, &size, &offset);
	if (size - offset >= 2048) return true;
	if (!WarnedCmdBuf) { WarnedCmdBuf = true; Printf("C3D: command buffer full, some draws of this frame are dropped\n"); }
	return false;
}

// What SetupAttributes gave the GPU for the last draw (for the diagnostics).
static uint64_t LastPermutation;
static int LastAttribCount, LastBufResult;

// Shadow copies of the GPU state the last draw set. citro3d re-sends every piece of
// state it is handed, changed or not, so Apply only hands it what changed since the
// previous draw. Everything is forgotten at the end of each frame (ResetDeviceState),
// because other code (the presenter, diagnostics) changes GPU state between frames.
struct StateCache
{
	bool valid = false;
	GPU_BLENDEQUATION blendEq; GPU_BLENDFACTOR blendSrc, blendDst;
	C3D_TexEnv env[6];
	bool alphaOn; int alphaRef;
	bool depthOn; GPU_TESTFUNC depthFunc; GPU_WRITEMASK depthMask;
	bool stencilOn; int stencilRef, stencilOp;
	C3D_FVec mvp[4], texmat[2], colorscale, colormul;
	C3D_AttrInfo attr;
	C3D_BufInfo buf;
	float fixedTex[4], fixedColor[4];
};
static StateCache Shadow;

void C3DRenderState::BeginFrame()
{
	if (IndexRing == nullptr)
	{
		IndexRing = (uint16_t *)linearAlloc(IndexRingSize * sizeof(uint16_t));
	}
	IndexRingPos = 0;
}

void C3DRenderState::ResetDeviceState()
{
	Shadow.valid = false;
	mBoundTexture = nullptr;
	mLastMaterial = nullptr;
	mLastClamp = mLastTranslation = -1;
}

//==========================================================================
//
// Immediate state
//
//==========================================================================

bool C3DRenderState::SetDepthClamp(bool on)
{
	return true;	// the PICA200 has no depth clamp; skyboxes are drawn without depth writes
}

void C3DRenderState::SetDepthMask(bool on)
{
	mDepthMask = on;
}

void C3DRenderState::SetDepthFunc(int func)
{
	mDepthFunc = func;
}

void C3DRenderState::SetDepthRange(float min, float max)
{
	// Used by portals to push geometry to the far plane: not supported yet.
}

void C3DRenderState::SetColorMask(bool r, bool g, bool b, bool a)
{
	mColorWrite = (r ? GPU_WRITE_RED : 0) | (g ? GPU_WRITE_GREEN : 0) | (b ? GPU_WRITE_BLUE : 0) | (a ? GPU_WRITE_ALPHA : 0);
}

void C3DRenderState::SetStencil(int offs, int op, int flags)
{
	mStencilRef = screen->stencilValue + offs;
	mStencilOp = op;
	if (flags != -1)
	{
		mStencilFlags = flags;
		SetColorMask(!(flags & SF_ColorMaskOff));
		SetDepthMask(!(flags & SF_DepthMaskOff));
	}
}

void C3DRenderState::EnableStencil(bool on)
{
	mStencilEnabled = on;
}

void C3DRenderState::SetCulling(int mode)
{
	mCulling = mode;
}

void C3DRenderState::EnableDepthTest(bool on)
{
	mDepthTest = on;
}

void C3DRenderState::SetScissor(int x, int y, int w, int h)
{
	Get()->ScreenBegin();
	if (Get()->IsFrameBlocked()) return;
	if (w < 0)
	{
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		return;
	}
	// Engine coordinates have their origin at the bottom left of the 400x240 screen; the top
	// screen's frame buffer is 240x400, turned 90 degrees.
	const int W = Get()->GetClientWidth();
	int left = y, top = W - x - w, right = y + h, bottom = W - x;
	ClampToFrameBuffer(left, top, right, bottom);
	C3D_SetScissor(GPU_SCISSOR_NORMAL, left, top, right, bottom);
#ifdef __3DS__
	Diag::MenuDiagRecordScissor(left, top, right, bottom);
#endif
}

void C3DRenderState::SetViewport(int x, int y, int w, int h)
{
	Get()->ScreenBegin();
	if (Get()->IsFrameBlocked()) return;
	const int W = Get()->GetClientWidth();
	// The 3D view of a game with the status bar reaches past the top of the screen on purpose
	// (the engine relies on the window to cut it off) and the console draws it right. What the
	// PICA200 cannot take is a negative origin: its registers are unsigned and the rectangle
	// wraps around (test 44: a 640x400 viewport drew the menu enlarged and repeated).
	int left = y, top = W - x - w, width = h, height = w;
	if (left < 0 || top < 0)
	{
		if (!WarnedViewport)
		{
			WarnedViewport = true;
			Printf("C3D: viewport %d,%d %dx%d starts outside the 400x240 screen, moved inside\n", x, y, w, h);
		}
		if (left < 0) { width += left; left = 0; }
		if (top < 0) { height += top; top = 0; }
	}
	if (width < 1 || height < 1) return;
	C3D_SetViewport(left, top, width, height);
}

void C3DRenderState::Clear(int targets)
{
	Get()->ScreenBegin();
	if (Get()->IsFrameBlocked()) return;
	C3D_ClearBits bits = (C3D_ClearBits)0;
	if (targets & CT_Color) bits = (C3D_ClearBits)(bits | C3D_CLEAR_COLOR);
	if (targets & (CT_Depth | CT_Stencil)) bits = (C3D_ClearBits)(bits | C3D_CLEAR_DEPTH);
	const float *c = screen->mSceneClearColor;
	const uint32_t color = (uint32_t(c[0] * 255.f) << 24) | (uint32_t(c[1] * 255.f) << 16) | (uint32_t(c[2] * 255.f) << 8) | 0xff;
	C3D_RenderTargetClear(Get()->Target(), bits, color, 0);
}

void C3DRenderState::ClearScreen()
{
	Clear(CT_Color | CT_Depth);
}

//==========================================================================
//
// Material
//
//==========================================================================

void C3DRenderState::ApplyMaterial()
{
	FMaterial *mat = mMaterial.mMaterial;
	mMaterial.mChanged = false;
	if (mat == nullptr)
	{
		mBoundTexture = nullptr;
		mLastMaterial = nullptr;
		return;
	}
	auto tex = mat->Source();
	if (tex->isHardwareCanvas()) static_cast<FCanvasTexture *>(tex->GetTexture())->NeedUpdate();
	const int clampmode = tex->GetClampMode(mMaterial.mClampMode);
	const int translation = mMaterial.mTranslation;
	if (mat == mLastMaterial && clampmode == mLastClamp && translation == mLastTranslation) return;
	mLastMaterial = mat;
	mLastClamp = clampmode;
	mLastTranslation = translation;

	MaterialLayerInfo *layer;
	auto base = static_cast<C3DHardwareTexture *>(mat->GetLayer(0, translation, &layer));
	if (base != nullptr && base->BindOrCreate(tex->GetTexture(), 0, clampmode, translation, layer->scaleFlags & ~CTF_Indexed))
	{
		mBoundTexture = base;
	}
	else
	{
		mBoundTexture = nullptr;
	}
	mTexClamp = clampmode;
}

//==========================================================================
//
// Blending
//
//==========================================================================

void C3DRenderState::ApplyBlendMode()
{
	static const GPU_BLENDFACTOR factors[] = {
		GPU_ZERO, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_COLOR, GPU_ONE_MINUS_SRC_COLOR,
		GPU_DST_COLOR, GPU_ONE_MINUS_DST_COLOR, GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA };

	GPU_BLENDFACTOR src = factors[mRenderStyle.SrcAlpha % STYLEALPHA_MAX];
	GPU_BLENDFACTOR dst = factors[mRenderStyle.DestAlpha % STYLEALPHA_MAX];
	GPU_BLENDEQUATION eq = GPU_BLEND_ADD;
	switch (mRenderStyle.BlendOp & 15)
	{
	case STYLEOP_Add: eq = GPU_BLEND_ADD; break;
	case STYLEOP_Sub: eq = GPU_BLEND_SUBTRACT; break;
	case STYLEOP_RevSub: eq = GPU_BLEND_REVERSE_SUBTRACT; break;
	case 0: break;
	default:	// fuzz styles
		src = GPU_DST_COLOR;
		dst = GPU_ONE_MINUS_SRC_ALPHA;
		break;
	}
	if (Shadow.valid && Shadow.blendEq == eq && Shadow.blendSrc == src && Shadow.blendDst == dst) return;
	Shadow.blendEq = eq; Shadow.blendSrc = src; Shadow.blendDst = dst;
	C3D_AlphaBlend(eq, eq, src, dst, src, dst);
}

//==========================================================================
//
// Texture combiners: the desktop shaders' texture modes, object color and
// add color, on the PICA200's fixed-function stages.
//
//==========================================================================

static uint32_t PackColor(const FVector4PalEntry &c)
{
	auto clamp = [](float v) { return (uint32_t)std::clamp(int(v * 255.f + 0.5f), 0, 255); };
	return clamp(c.r) | (clamp(c.g) << 8) | (clamp(c.b) << 16) | (clamp(c.a) << 24);
}

void C3DRenderState::ApplyCombiners(bool textured, int texMode)
{
	C3D_TexEnv envs[6];
	memset(envs, 0, sizeof(envs));
	C3D_TexEnv *env = &envs[0];
	C3D_TexEnvInit(env);
	if (!textured)
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		switch (texMode)
		{
		default:
		case TM_NORMAL:
		case TM_CLAMPY:
			break;
		case TM_STENCIL:	// (1, 1, 1, a)
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
			break;
		case TM_OPAQUE:		// (r, g, b, 1)
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
			break;
		case TM_INVERSE:	// (1-r, 1-g, 1-b, a)
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
			break;
		case TM_ALPHATEXTURE:	// (1, 1, 1, r)
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
			C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_R, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
			break;
		case TM_INVERTOPAQUE:	// (1-r, 1-g, 1-b, 1)
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
			break;
		case TM_FOGLAYER:	// the shape of the texture in the vertex color
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
			break;
		}
	}

	// Stage 1: the object color (multiplied in).
	env = &envs[1];
	C3D_TexEnvInit(env);
	const uint32_t objectColor = PackColor(mStreamData.uObjectColor);
	if (objectColor != 0xffffffff && texMode != TM_FIXEDCOLORMAP)
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		C3D_TexEnvColor(env, objectColor);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}

	// Stage 2: the add color.
	env = &envs[2];
	C3D_TexEnvInit(env);
	const uint32_t addColor = PackColor(mStreamData.uAddColor) & 0xffffff;
	if (addColor != 0 && texMode != TM_FIXEDCOLORMAP)
	{
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		C3D_TexEnvColor(env, addColor);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	for (int i = 3; i < 6; i++)
	{
		env = &envs[i];
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	for (int i = 0; i < 6; i++)
	{
		if (Shadow.valid && memcmp(&Shadow.env[i], &envs[i], sizeof(C3D_TexEnv)) == 0) continue;
		*C3D_GetTexEnv(i) = envs[i];
		Shadow.env[i] = envs[i];
	}
}

//==========================================================================
//
// Matrices: projection * view * model, the screen's tilt and the PICA200's
// depth range ([-w, 0] instead of OpenGL's [-w, w]).
//
//==========================================================================

static void Multiply(const float *a, const float *b, float *out)	// column-major 4x4: out = a * b
{
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
			out[c * 4 + r] = s;
		}
}

void C3DRenderState::ApplyMatrices()
{
	float pv[16], mvp[16];
	if (mHwUniforms != nullptr)
	{
		Multiply(mHwUniforms->ProjectionMatrix.get(), mHwUniforms->ViewMatrix.get(), pv);
	}
	else
	{
		VSMatrix identity(0);
		memcpy(pv, identity.get(), sizeof(pv));
	}
	if (mModelMatrixEnabled) Multiply(pv, mModelMatrix.get(), mvp);
	else memcpy(mvp, pv, sizeof(mvp));

	// Rows of the clip-space transform (element (r, c) = mvp[c * 4 + r]).
	auto row = [&](int r, float out[4]) { for (int c = 0; c < 4; c++) out[c] = mvp[c * 4 + r]; };
	float rx[4], ry[4], rz[4], rw[4];
	row(0, rx); row(1, ry); row(2, rz); row(3, rw);

	C3D_FVec u[4];
	// Tilt: the frame buffer's x is the screen's y, its y the screen's -x.
	u[0] = FVec4_New(ry[0], ry[1], ry[2], ry[3]);
	u[1] = FVec4_New(-rx[0], -rx[1], -rx[2], -rx[3]);
	u[2] = FVec4_New(0.5f * rz[0] - 0.5f * rw[0], 0.5f * rz[1] - 0.5f * rw[1], 0.5f * rz[2] - 0.5f * rw[2], 0.5f * rz[3] - 0.5f * rw[3]);
	u[3] = FVec4_New(rw[0], rw[1], rw[2], rw[3]);

	C3D_FVec t[2] = { FVec4_New(1, 0, 0, 0), FVec4_New(0, 1, 0, 0) };
	if (mTextureMatrixEnabled)
	{
		const float *m = mTextureMatrix.get();
		t[0] = FVec4_New(m[0], m[4], 0, m[12]);
		t[1] = FVec4_New(m[1], m[5], 0, m[13]);
	}
	else if (c3d_wipeorient != -1 && mBoundTexture != nullptr && mBoundTexture->IsScreenCopy())
	{
#ifdef __3DS__
		if (c3d_wipeorient == 8)
		{
			static clock_t diagStartTime = 0;
			clock_t now = clock();
			if (diagStartTime == 0)
			{
				diagStartTime = now;
				int lw = mBoundTexture->GetWidth();
				int lh = mBoundTexture->GetHeight();
				Printf("[WIPE] diag %dx%d, UV adjustment: no\n", lw, lh);
			}

			double elapsed = (double)(now - diagStartTime) / CLOCKS_PER_SEC;
			if (elapsed < 3.0)
			{
				float sx = 1.0f, sy = 1.0f; // Identity for identity matrix
				t[0] = FVec4_New(sx, 0, 0, 0);
				t[1] = FVec4_New(0, sy, 0, 0);
			}
			else
			{
				diagStartTime = 0;
			}
		}
		else
#endif
		{
			float sx = 240.f / 256.f;
			float sy = 400.f / 512.f;

			int orient = c3d_wipeorient;
			switch (orient)
			{
			case 0: // No rotation
				t[0] = FVec4_New(sx, 0, 0, 0);
				t[1] = FVec4_New(0, sy, 0, 0);
				break;
			case 1: // 90 deg CW
				t[0] = FVec4_New(0, sx, 0, 0);
				t[1] = FVec4_New(-sy, 0, 0, sy);
				break;
			case 2: // 180 deg
				t[0] = FVec4_New(-sx, 0, 0, sx);
				t[1] = FVec4_New(0, -sy, 0, sy);
				break;
			case 3: // 270 deg CW
				t[0] = FVec4_New(0, -sx, 0, sx);
				t[1] = FVec4_New(sy, 0, 0, 0);
				break;
			case 4:
				t[0] = FVec4_New(0, sx, 0, 0);
				t[1] = FVec4_New(sy, 0, 0, 0);
				break;
			case 5:
				t[0] = FVec4_New(0, -sx, 0, sx);
				t[1] = FVec4_New(-sy, 0, 0, sy);
				break;
			case 6:
				t[0] = FVec4_New(-sx, 0, 0, sx);
				t[1] = FVec4_New(0, sy, 0, 0);
				break;
			case 7:
				t[0] = FVec4_New(sx, 0, 0, 0);
				t[1] = FVec4_New(0, -sy, 0, sy);
				break;
			default:
				t[0] = FVec4_New(sx, 0, 0, 0);
				t[1] = FVec4_New(0, sy, 0, 0);
				break;
			}
		}
	}
	else
	{
		t[0] = FVec4_New(1, 0, 0, 0);
		t[1] = FVec4_New(0, 1, 0, 0);
	}
	if (!Shadow.valid || memcmp(Shadow.mvp, u, sizeof(u)) != 0)
	{
		memcpy(Shadow.mvp, u, sizeof(u));
		memcpy(C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.mvp, 4), u, sizeof(u));
	}
	if (!Shadow.valid || memcmp(Shadow.texmat, t, sizeof(t)) != 0)
	{
		memcpy(Shadow.texmat, t, sizeof(t));
		memcpy(C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.texmat, 2), t, sizeof(t));
	}
}

//==========================================================================
//
// Vertex attributes from the engine's buffer layout
//
//==========================================================================

bool C3DRenderState::SetupAttributes(int baseVertex)
{
	auto vb = static_cast<C3DVertexBuffer *>(mVertexBuffer);
	if (vb == nullptr || vb->Memory() == nullptr) return false;

	// Built locally and handed to citro3d only when different from the last draw's.
	C3D_AttrInfo aiLocal;
	memset(&aiLocal, 0, sizeof(aiLocal));
	C3D_AttrInfo *ai = &aiLocal;
	AttrInfo_Init(ai);
	C3D_BufInfo biLocal;
	memset(&biLocal, 0, sizeof(biLocal));
	C3D_BufInfo *bi = &biLocal;
	BufInfo_Init(bi);
	float fixedTex[4] = { -1, -1, -1, -1 }, fixedColor[4] = { -1, -1, -1, -1 };
	int fixedTexIndex = -1, fixedColorIndex = -1;

	// Loaders, in the buffer's order (sorted by offset), with padding between them.
	uint64_t permutation = 0;
	int count = 0, pos = 0, loaders = 0;
	bool haveTex = false, haveColor = false;
	int attrIndex[3] = { -1, -1, -1 };
	auto addPadding = [&](int bytes)
	{
		while (bytes > 0)
		{
			const int chunk = std::min(bytes, 16);
			permutation |= uint64_t(0xB + chunk / 4) << (count * 4);	// 0xC..0xF: 4..16 bytes of padding
			count++;
			bytes -= chunk;
		}
	};
	for (auto &a : vb->mAttributes)
	{
		int reg, format, comps, size;
		switch (a.location)
		{
		case VATTR_VERTEX: reg = 0; break;
		case VATTR_TEXCOORD: reg = 1; break;
		default: reg = 2; break;
		}
		switch (a.format)
		{
		case VFmt_Float4: format = GPU_FLOAT; comps = 4; size = 16; break;
		case VFmt_Float3: format = GPU_FLOAT; comps = 3; size = 12; break;
		case VFmt_Float2: format = GPU_FLOAT; comps = 2; size = 8; break;
		case VFmt_Float: format = GPU_FLOAT; comps = 1; size = 4; break;
		case VFmt_Byte4: format = GPU_UNSIGNED_BYTE; comps = 4; size = 4; break;
		default: continue;
		}
		if (a.offset < pos) continue;
		addPadding(a.offset - pos);
		const int index = AttrInfo_AddLoader(ai, reg, (GPU_FORMATS)format, comps);
		attrIndex[reg] = index;
		permutation |= uint64_t(index) << (count * 4);
		count++;
		loaders++;
		pos = a.offset + size;
		if (reg == 1) haveTex = true;
		if (reg == 2) haveColor = true;
	}
	if (attrIndex[0] < 0) return false;

	// Pad the permutation to cover the full stride. The PICA200 requires that the
	// sum of attribute and padding sizes in the permutation exactly equals the
	// buffer's stride, otherwise it freezes [GBATEK "3DS GPU Pitfalls"].
	if (pos < vb->mStride) addPadding(vb->mStride - pos);

	// What the buffer lacks comes from fixed attributes.
	if (!haveTex)
	{
		fixedTexIndex = AttrInfo_AddFixed(ai, 1);
		fixedTex[0] = fixedTex[1] = fixedTex[2] = fixedTex[3] = 0;
	}
	C3D_FVec csLocal, cmLocal;
	C3D_FVec *cs = &csLocal;
	C3D_FVec *cm = &cmLocal;
	// The software light level (0..1) darkens the whole surface; distance falls off with the fog.
	float light = 1.f;
	if (mLightParms[3] >= 0.f) light = std::clamp(mLightParms[3], 0.f, 1.f);
	if (haveColor)
	{
		*cs = FVec4_New(1.f / 255.f, 1.f / 255.f, 1.f / 255.f, 1.f / 255.f);
		*cm = FVec4_New(light, light, light, 1.f);
	}
	else
	{
		fixedColorIndex = AttrInfo_AddFixed(ai, 2);
		const auto &c = mStreamData.uVertexColor;
		fixedColor[0] = c.X; fixedColor[1] = c.Y; fixedColor[2] = c.Z; fixedColor[3] = c.W;
		*cs = FVec4_New(1.f, 1.f, 1.f, 1.f);
		*cm = FVec4_New(light, light, light, 1.f);
	}

	uint8_t *base = (uint8_t *)vb->Memory() + size_t(baseVertex) * vb->mStride;
	const int bufInfoResult = BufInfo_Add(bi, base, vb->mStride, count, permutation);
	LastPermutation = permutation;
	LastAttribCount = count;
	LastBufResult = bufInfoResult;
	if (bufInfoResult < 0) return false;	// -2: the vertices are not in memory the GPU can read

	const bool attrChanged = !Shadow.valid || memcmp(&Shadow.attr, &aiLocal, sizeof(aiLocal)) != 0;
	if (attrChanged)
	{
		Shadow.attr = aiLocal;
		*C3D_GetAttrInfo() = aiLocal;
	}
	if (!Shadow.valid || memcmp(&Shadow.buf, &biLocal, sizeof(biLocal)) != 0)
	{
		Shadow.buf = biLocal;
		*C3D_GetBufInfo() = biLocal;
	}
	// Fixed attributes: re-set when their slot or value changed.
	if (fixedTexIndex >= 0 && (attrChanged || memcmp(Shadow.fixedTex, fixedTex, sizeof(fixedTex)) != 0))
	{
		memcpy(Shadow.fixedTex, fixedTex, sizeof(fixedTex));
		C3D_FixedAttribSet(fixedTexIndex, fixedTex[0], fixedTex[1], fixedTex[2], fixedTex[3]);
	}
	if (fixedColorIndex >= 0 && (attrChanged || memcmp(Shadow.fixedColor, fixedColor, sizeof(fixedColor)) != 0))
	{
		memcpy(Shadow.fixedColor, fixedColor, sizeof(fixedColor));
		C3D_FixedAttribSet(fixedColorIndex, fixedColor[0], fixedColor[1], fixedColor[2], fixedColor[3]);
	}
	if (!Shadow.valid || memcmp(&Shadow.colorscale, cs, sizeof(C3D_FVec)) != 0)
	{
		Shadow.colorscale = *cs;
		*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colorscale, 1) = *cs;
	}
	if (!Shadow.valid || memcmp(&Shadow.colormul, cm, sizeof(C3D_FVec)) != 0)
	{
		Shadow.colormul = *cm;
		*C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colormul, 1) = *cm;
	}
	Shadow.valid = true;	// every cached piece now matches what citro3d holds
	return true;
}

//==========================================================================
//
// Draw
//
//==========================================================================

bool C3DRenderState::Apply(int dt)
{
	Get()->ScreenBegin();
	if (Get()->IsFrameBlocked()) return false;

	if (mMaterial.mChanged) ApplyMaterial();
	ApplyBlendMode();

	const bool textured = mTextureEnabled && mBoundTexture != nullptr && mBoundTexture->GetTex() != nullptr && !Diag::NoTex;
	if (textured) C3D_TexBind(0, mBoundTexture->GetTex());

	int texMode = mTextureMode;
	if (texMode < 0) texMode = TM_NORMAL;
	ApplyCombiners(textured, texMode);

	{
		const bool alphaOn = mAlphaThreshold >= 0.f && textured;
		const int alphaRef = alphaOn ? std::clamp(int(mAlphaThreshold * 255.f), 0, 255) : 0;
		if (!Shadow.valid || Shadow.alphaOn != alphaOn || Shadow.alphaRef != alphaRef)
		{
			Shadow.alphaOn = alphaOn; Shadow.alphaRef = alphaRef;
			if (alphaOn) C3D_AlphaTest(true, GPU_GREATER, alphaRef);
			else C3D_AlphaTest(false, GPU_ALWAYS, 0);
		}
	}

	static const GPU_TESTFUNC depthFuncs[] = { GPU_GREATER, GPU_GEQUAL, GPU_ALWAYS };
	const GPU_WRITEMASK writeMask = (GPU_WRITEMASK)(mColorWrite | (mDepthMask ? GPU_WRITE_DEPTH : 0));
	{
		const bool depthOn = mDepthTest;
		const GPU_TESTFUNC depthFunc = depthFuncs[std::clamp(mDepthFunc, 0, 2)];
		if (!Shadow.valid || Shadow.depthOn != depthOn || Shadow.depthFunc != depthFunc || Shadow.depthMask != writeMask)
		{
			Shadow.depthOn = depthOn; Shadow.depthFunc = depthFunc; Shadow.depthMask = writeMask;
			C3D_DepthTest(depthOn, depthFunc, writeMask);
		}
	}
#ifdef __3DS__
	if (mBias.mChanged)
	{
		C3D_DepthMap(true, -1.0f, mBias.mUnits * c3d_depthbias);
		static bool logged = false;
		if (!logged) { Printf("c3d_depthbias used: %f\n", *c3d_depthbias); logged = true; }
		mBias.mChanged = false;
	}
#endif
	if (!Shadow.valid) C3D_CullFace(GPU_CULL_NONE);

	{
		const bool stencilOn = mStencilEnabled;
		const int stencilRef = stencilOn ? (mStencilRef & 0xff) : 0;
		const int stencilOp = stencilOn ? std::clamp(mStencilOp, 0, 2) : 0;
		if (!Shadow.valid || Shadow.stencilOn != stencilOn || Shadow.stencilRef != stencilRef || Shadow.stencilOp != stencilOp)
		{
			Shadow.stencilOn = stencilOn; Shadow.stencilRef = stencilRef; Shadow.stencilOp = stencilOp;
			if (stencilOn)
			{
				static const GPU_STENCILOP ops[] = { GPU_STENCIL_KEEP, GPU_STENCIL_INCR, GPU_STENCIL_DECR };
				C3D_StencilTest(true, GPU_EQUAL, stencilRef, 0xff, 0xff);
				C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, ops[stencilOp]);
			}
			else
			{
				C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
			}
		}
	}

	ApplyMatrices();
	return true;
}

static GPU_Primitive_t Primitive(int dt)
{
	switch (dt)
	{
	case DT_TriangleFan: return GPU_TRIANGLE_FAN;
	case DT_TriangleStrip: return GPU_TRIANGLE_STRIP;
	default: return GPU_TRIANGLES;
	}
}

// The first draws in full: what the GPU is asked to read (diagnostics, see c3d_diag.h).
void C3DRenderState::DescribeDraw(const char *kind, int dt, int first, int count, const void *vertices, const void *indices)
{
	auto vb = static_cast<C3DVertexBuffer *>(mVertexBuffer);
	char attrs[160] = "";
	size_t len = 0;
	for (auto &a : vb->mAttributes)
		len += snprintf(attrs + len, len < sizeof(attrs) ? sizeof(attrs) - len : 0, "%s%d:%d@%d", len ? "," : "", a.location, a.format, a.offset);
	const bool textured = mTextureEnabled && mBoundTexture != nullptr && mBoundTexture->GetTex() != nullptr && !Diag::NoTex;
	C3D_Tex *tex = textured ? mBoundTexture->GetTex() : nullptr;
	Diag::Line("draw %s prim=%d first=%d count=%d stride=%d attrs=[%s] perm=%llx n=%d buf=%d "
		"vtx_pa=%08lx idx_pa=%08lx tex=%dx%d fmt=%d mode=%d blend=%d/%d/%d alpha=%.2f depth=%d stencil=%d",
		kind, dt, first, count, (int)vb->mStride, attrs, (unsigned long long)LastPermutation, LastAttribCount, LastBufResult,
		(unsigned long)osConvertVirtToPhys(vertices), (unsigned long)(indices ? osConvertVirtToPhys(indices) : 0),
		tex ? tex->width : 0, tex ? tex->height : 0, tex ? (int)tex->fmt : -1, mTextureMode,
		mRenderStyle.BlendOp, mRenderStyle.SrcAlpha, mRenderStyle.DestAlpha, mAlphaThreshold, mDepthTest, mStencilEnabled);
}

void C3DRenderState::Draw(int dt, int index, int count, bool apply)
{
	if (dt == DT_Lines || dt == DT_Points)
	{
		if (!WarnedLines) { WarnedLines = true; Printf("C3D: lines and points are not drawn yet\n"); }
		return;
	}
	if (count <= 0 || !Apply(dt)) return;
	if (!SetupAttributes(mVertexOffsets[0])) return;

	auto vb = static_cast<C3DVertexBuffer *>(mVertexBuffer);
	uint8_t *first = (uint8_t *)vb->Memory() + size_t(mVertexOffsets[0] + index) * vb->mStride;

	const bool send = !Diag::NoDraw && !Diag::Triangle;
	if (Diag::WantDrawDetails()) DescribeDraw(send ? "arrays" : "arrays(not sent)", dt, index, count, first, nullptr);
	Diag::Draw(false, send);
	if (send && CommandBufferHasRoom()) C3D_DrawArrays(Primitive(dt), index, count);
}

void C3DRenderState::DrawIndexed(int dt, int index, int count, bool apply)
{
	if (dt == DT_Lines || dt == DT_Points || count <= 0) return;
	auto ib = static_cast<C3DIndexBuffer *>(mIndexBuffer);
	if (ib == nullptr || ib->Memory() == nullptr || IndexRing == nullptr) return;
	if (IndexRingPos + count > IndexRingSize) return;	// out of index space this frame

	const uint32_t *src = (const uint32_t *)ib->Memory() + index;
	uint32_t lo = src[0], hi = src[0];
	for (int i = 1; i < count; i++) { lo = std::min(lo, src[i]); hi = std::max(hi, src[i]); }
	if (hi - lo > 0xffff) return;	// would need splitting; never seen in the engine's draws

	if (!Apply(dt)) return;
	const int base = mVertexOffsets[0] + int(lo);
	if (!SetupAttributes(base)) return;

	auto vb = static_cast<C3DVertexBuffer *>(mVertexBuffer);
	uint16_t *dst = IndexRing + IndexRingPos;
	for (int i = 0; i < count; i++) dst[i] = uint16_t(src[i] - lo);
	IndexRingPos += (count + 1) & ~1;	// keep the next block 4-byte aligned
	uint8_t *first = (uint8_t *)vb->Memory() + size_t(base) * vb->mStride;

	const bool send = !Diag::NoDraw && !Diag::Triangle;
	if (Diag::WantDrawDetails()) DescribeDraw(send ? "elements" : "elements(not sent)", dt, int(lo), count, first, dst);
	Diag::Draw(true, send);
	if (send && CommandBufferHasRoom()) C3D_DrawElements(Primitive(dt), count, C3D_UNSIGNED_SHORT, dst);
}

} // namespace C3DRenderer
