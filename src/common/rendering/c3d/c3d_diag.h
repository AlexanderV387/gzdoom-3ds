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
**   c3d-testflip.txt      two coloured quads via engine 2D path: red at (0,0), green at bottom
**   c3d-menu-diag.txt     log viewport, geometry, textures and scissor for menu vs HUD frames
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
extern bool TestFlip;
extern bool MenuDiag;
extern bool NoFlush;

void Init();									// reads the switches, logs them
void Line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

bool HangReported();
int FrameN();										// current frame number
void FrameBegun(bool ok, int retries);			// after C3D_FrameBegin (and its retries)
void FrameDone(bool drawn);						// end of Update: one summary line per frame
void Draw(bool indexed, bool sent);				// every draw call
bool WantDrawDetails();							// true for the first draws only
bool MenuDiagWantFrame();						// true if this Draw2D frame should be logged for menu diag
void MenuDiagRecordScissor(int l, int t, int r, int b);	// record one scissor call
bool MenuDiagEmitScissor();						// log scissor summary for current frame, returns true if logged

// Per-frame timing breakdown (test 52): measure elapsed time per category per frame
// test 55: TC_HUD2D split into TC_HDHud + TC_HDGPU; ot broken into 4 categories
enum TimingCategory {
    TC_Logic = 0, TC_Scene3D, TC_HDHud, TC_HDGPU, TC_GPUWait, TC_Texture,
    TC_Music, TC_Input, TC_Sound, TC_PreDisplay, TC_FrameEnd,
    TC_Loop, TC_GC, TC_Net, TC_Wait,
    TC_COUNT
};
void TimingBegin(int category);	// mark start: stores svcGetSystemTick() internally
void TimingEnd(int category);	// mark end: adds elapsed ticks if begin was called, then clears
extern int TimingTicsWindow;	// tics executed per window (accumulated from d_net.cpp)
extern int Timing2DDrawsWindow;	// 2D draw calls per window (counted in Draw2D)

}
}
