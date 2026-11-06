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

}
