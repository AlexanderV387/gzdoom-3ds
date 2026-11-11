/*
** c3d_diag.h
** Diagnostics for the citro3d backend (test 25): switches read from the SD card,
** a summary line per frame, the details of the first draws, and a single
** "GPU HANG" line when a frame's GPU work never finishes.
**
** Switches (empty files in sdmc:/3ds/gzdoom/, read once at start):
**   c3d-nodraw.txt        everything as usual, but no draw command reaches the GPU
**   c3d-notex.txt         draws without textures (vertex colour only)
**   c3d-triangle.txt      instead of the engine's 2D, one fixed red triangle per frame
**   c3d-minimal-shader.txt use the simple_tri shader (not the engine's) for the test triangle
**   c3d-v0.txt ... c3d-v3.txt test 29 variants (V0=dp4 intex direct no zw, V1=dp4 r2 with zw,
**                                              V2=dp4 intex direct with zw, V3=original baseline)
**
** All lines start with "[C3D]" and are flushed to the SD card at once.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#pragma once

#include <3ds.h>
#include <citro3d.h>

namespace C3DRenderer
{
namespace Diag
{

extern bool NoDraw;
extern bool NoTex;
extern bool Triangle;
extern bool MinimalShader;
extern int Test29Variant;			// -1 = no variant switch; 0-3 = V0-V3

void Init();									// reads the switches, logs them
void Line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

bool HangReported();
void FrameBegun(bool ok, int retries);			// after C3D_FrameBegin (and its retries)
void FrameDone(bool drawn);						// end of Update: one summary line per frame
void Draw(bool indexed, bool sent);				// every draw call
bool WantDrawDetails();							// true for the first draws only

}
}
