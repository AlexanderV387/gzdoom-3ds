/*
** c3d_internal.h
** Shared by the citro3d backend's files: the vertex shader's uniforms.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#pragma once

namespace C3DRenderer
{

struct ShaderUniforms
{
	int mvp = -1;
	int colorscale = -1;
	int colormul = -1;
	int texmat = -1;
};

extern ShaderUniforms Uniforms;

// The PICA200 reads vertex data and textures until the frame it was drawn in has finished:
// their memory is freed at the start of the next frame (C3D_FRAME_SYNCDRAW waits for it).
void DeferLinearFree(void *ptr);
void DeferTexDelete(const C3D_Tex &tex);
void RunDeferredFrees();



} // namespace C3DRenderer
