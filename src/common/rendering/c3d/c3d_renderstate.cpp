/*
** c3d_renderstate.cpp
** FRenderState for the citro3d backend: turns the engine's render state into
** PICA200 state (combiners, blending, alpha test, depth, stencil) and draws.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <string.h>
#include <algorithm>
#include <string>

#include "c3d_backend.h"
#include "c3d_internal.h"
#include "hw_material.h"
#include "texturemanager.h"
#include "printf.h"
#include "platform_3ds.h"

namespace C3DRenderer
{

// Index ring: the engine's 32-bit indices are narrowed to the PICA200's 16 bits here.
static uint16_t *IndexRing = nullptr;
static size_t IndexRingPos = 0;
static const size_t IndexRingSize = 512 * 1024;	// indices per frame

static bool WarnedLines = false;



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
	#ifdef __3DS__
	if (Get()->IsFrameBlocked()) return;
	#endif
	if (w < 0)
	{
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		return;
	}
	// Engine coordinates have their origin at the bottom left of the 400x240 screen; the top
	// screen's frame buffer is 240x400, turned 90 degrees.
	const int W = Get()->GetClientWidth();
	C3D_SetScissor(GPU_SCISSOR_NORMAL, y, W - x - w, y + h, W - x);
}

void C3DRenderState::SetViewport(int x, int y, int w, int h)
{
	Get()->ScreenBegin();
	#ifdef __3DS__
	if (Get()->IsFrameBlocked()) return;
	#endif
	const int W = Get()->GetClientWidth();
	C3D_SetViewport(y, W - x - w, h, w);
}

void C3DRenderState::Clear(int targets)
{
	Get()->ScreenBegin();
	#ifdef __3DS__
	if (Get()->IsFrameBlocked()) return;
	#endif
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
	C3D_TexEnv *env = C3D_GetTexEnv(0);
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
	env = C3D_GetTexEnv(1);
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
	env = C3D_GetTexEnv(2);
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
		env = C3D_GetTexEnv(i);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
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

	C3D_FVec *u = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.mvp, 4);
	// Tilt: the frame buffer's x is the screen's y, its y the screen's -x.
	u[0] = FVec4_New(ry[0], ry[1], ry[2], ry[3]);
	u[1] = FVec4_New(-rx[0], -rx[1], -rx[2], -rx[3]);
	u[2] = FVec4_New(0.5f * rz[0] - 0.5f * rw[0], 0.5f * rz[1] - 0.5f * rw[1], 0.5f * rz[2] - 0.5f * rw[2], 0.5f * rz[3] - 0.5f * rw[3]);
	u[3] = FVec4_New(rw[0], rw[1], rw[2], rw[3]);

	C3D_FVec *t = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.texmat, 2);
	if (mTextureMatrixEnabled)
	{
		const float *m = mTextureMatrix.get();
		t[0] = FVec4_New(m[0], m[4], 0, m[12]);
		t[1] = FVec4_New(m[1], m[5], 0, m[13]);
	}
	else
	{
		t[0] = FVec4_New(1, 0, 0, 0);
		t[1] = FVec4_New(0, 1, 0, 0);
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

	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);

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
		const int index = AttrInfo_AddFixed(ai, 1);
		C3D_FixedAttribSet(index, 0, 0, 0, 0);
	}
	C3D_FVec *cs = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colorscale, 1);
	C3D_FVec *cm = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, Uniforms.colormul, 1);
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
		const int index = AttrInfo_AddFixed(ai, 2);
		const auto &c = mStreamData.uVertexColor;
		C3D_FixedAttribSet(index, c.X, c.Y, c.Z, c.W);
		*cs = FVec4_New(1.f, 1.f, 1.f, 1.f);
		*cm = FVec4_New(light, light, light, 1.f);
	}

	uint8_t *base = (uint8_t *)vb->Memory() + size_t(baseVertex) * vb->mStride;
	int bufInfoResult = BufInfo_Add(bi, base, vb->mStride, count, permutation);


	if (bufInfoResult < 0)
	{
		return false;
	}
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
	#ifdef __3DS__
	if (Get()->IsFrameBlocked()) return false;
	#endif

	if (mMaterial.mChanged) ApplyMaterial();
	ApplyBlendMode();

	const bool textured = mTextureEnabled && mBoundTexture != nullptr && mBoundTexture->GetTex() != nullptr;
	if (textured) C3D_TexBind(0, mBoundTexture->GetTex());

	int texMode = mTextureMode;
	if (texMode < 0) texMode = TM_NORMAL;
	ApplyCombiners(textured, texMode);

	if (mAlphaThreshold >= 0.f && textured)
		C3D_AlphaTest(true, GPU_GREATER, std::clamp(int(mAlphaThreshold * 255.f), 0, 255));
	else
		C3D_AlphaTest(false, GPU_ALWAYS, 0);

	static const GPU_TESTFUNC depthFuncs[] = { GPU_GREATER, GPU_GEQUAL, GPU_ALWAYS };
	const GPU_WRITEMASK writeMask = (GPU_WRITEMASK)(mColorWrite | (mDepthMask ? GPU_WRITE_DEPTH : 0));
	C3D_DepthTest(mDepthTest, depthFuncs[std::clamp(mDepthFunc, 0, 2)], writeMask);
	C3D_CullFace(GPU_CULL_NONE);

	if (mStencilEnabled)
	{
		static const GPU_STENCILOP ops[] = { GPU_STENCIL_KEEP, GPU_STENCIL_INCR, GPU_STENCIL_DECR };
		C3D_StencilTest(true, GPU_EQUAL, mStencilRef & 0xff, 0xff, 0xff);
		C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, ops[std::clamp(mStencilOp, 0, 2)]);
	}
	else
	{
		C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
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

	GSPGPU_FlushDataCache((uint8_t *)vb->Memory() + size_t(mVertexOffsets[0] + index) * vb->mStride, size_t(count) * vb->mStride);
	C3D_DrawArrays(Primitive(dt), index, count);
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
	GSPGPU_FlushDataCache(dst, count * sizeof(uint16_t));

	GSPGPU_FlushDataCache((uint8_t *)vb->Memory() + size_t(base) * vb->mStride, size_t(hi - lo + 1) * vb->mStride);
	C3D_DrawElements(Primitive(dt), count, C3D_UNSIGNED_SHORT, dst);
}

} // namespace C3DRenderer
