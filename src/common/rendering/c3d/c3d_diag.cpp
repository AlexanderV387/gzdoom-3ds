/*
** c3d_diag.cpp
** Diagnostics for the citro3d backend (see c3d_diag.h).
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "c3d_diag.h"
#include "platform_3ds.h"
#include "menustate.h"
#include "gamestate.h"

namespace C3DRenderer
{
namespace Diag
{

bool NoDraw = false;
bool NoTex = false;
bool Triangle = false;
bool MinimalShader = false;
bool TestFlip = false;
bool MenuDiag = false;
bool NoFlush = false;

static const int SummaryFrames = 12;	// frames with a summary line
static const int DetailDraws = 8;		// draws described in full

static int Frame = 0;					// frames begun or attempted
static bool Hang = false;
static int BeginRetries = 0;
static bool BeginOk = false;
static int Arrays = 0, Elements = 0, NotSent = 0;
static int DrawsDescribed = 0;

// Menu diag: up to 20 menu frames and 3 HUD frames
static int MenuDiagMenuFrames = 0;
static int MenuDiagHudFrames = 0;

// Per-frame timing breakdown (test 52): 7 base + 6 new categories (test 55) measured with svcGetSystemTick (268.1 MHz)
// TC_Logic: game tics, TC_Scene3D: 3D CPU prep, TC_HDHud: HUD overlays, TC_HDGPU: 2D GPU send,
// TC_GPUWait: FrameBegin retries + FrameEnd, TC_Texture: texture uploads, TC_Music: S_ChangeMusic
// TC_Input: I_StartTic + D_ProcessEvents, TC_Sound: S_UpdateMusic,
// TC_PreDisplay: D_Display pre-scene, TC_FrameEnd: C3D_FrameEnd
static unsigned long long TimingBeginTicks[TC_COUNT] = { 0 }; // 0 means no begin yet this frame
static unsigned long long TimingFrame[TC_COUNT] = { 0 };
static unsigned long long TimingWorst[TC_COUNT] = { 0 };
static unsigned long long TimingWindow[TC_COUNT] = { 0 };
static int TimingTexWindow = 0; // texture upload count per window
static double TimingGPUWindow = 0; // real GPU time from citro3d per window (ms)
static double TimingCPUWindow = 0; // real CPU time from citro3d per window (ms)
int TimingTicsWindow = 0; // tics executed per window (for average)
int Timing2DDrawsWindow = 0; // 2D draw calls per window

static bool Exists(const char *name)
{
	char path[128];
	snprintf(path, sizeof(path), N3DS_DATA_DIR "/%s", name);
	struct stat st;
	return stat(path, &st) == 0;
}

void Line(const char *fmt, ...)
{
	char text[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(text, sizeof(text) - 2, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if (n > (int)sizeof(text) - 3) n = sizeof(text) - 3;
	N3DS_Log("[C3D] ");
	text[n] = '\n';
	text[n + 1] = 0;
	N3DS_Log(text);
	N3DS_LogFlush();
}

void Init()
{
	NoDraw = Exists("c3d-nodraw.txt");
	NoTex = Exists("c3d-notex.txt");
	Triangle = Exists("c3d-triangle.txt");
	MinimalShader = Exists("c3d-minimal-shader.txt");
	TestFlip = Exists("c3d-testflip.txt");
	MenuDiag = Exists("c3d-menu-diag.txt");
	NoFlush = Exists("c3d-noflush.txt");
	Line("switches: nodraw=%d notex=%d triangle=%d minshader=%d testflip=%d menudiag=%d noflush=%d",
		NoDraw, NoTex, Triangle, MinimalShader, TestFlip, MenuDiag, NoFlush);
}

bool HangReported()
{
	return Hang;
}

int FrameN()
{
	return Frame;
}

void FrameBegun(bool ok, int retries)
{
	BeginOk = ok;
	BeginRetries = retries;
	if (!ok && !Hang && retries >= 120)
	{
		Hang = true;
		Line("GPU HANG: frame %d could not start after %d vblanks (2 s): the GPU never finished frame %d",
			Frame, retries, Frame - 1);
	}
}

void FrameDone(bool drawn)
{
	// Accumulate real GPU/CPU times from citro3d (ms per completed frame)
	TimingGPUWindow += C3D_GetDrawingTime();
	TimingCPUWindow += C3D_GetProcessingTime();
	if (Frame < SummaryFrames)
	{
		Line("frame %d: begin=%s retries=%d draws: arrays=%d elements=%d not_sent=%d%s",
			Frame, BeginOk ? "ok" : "FAILED", BeginRetries, Arrays, Elements, NotSent,
			drawn ? (Triangle ? " (test triangle)" : "") : " (skipped: no frame)");
	}
	// Every 5 seconds: frames, average and worst frame time, draws per frame, and where
	// (gamestate, menu). One short line; the performance record of a session on the console.
	{
		static long long windowStart = -1, last = -1;
		static int frames = 0, draws = 0, worst = 0;
		const long long now = N3DS_ElapsedMs();
		if (windowStart < 0) windowStart = last = now;
		frames++;
		draws += Arrays + Elements;
		worst = std::max(worst, int(now - last));
		last = now;
		if (now - windowStart >= 5000)
		{
			char buf[512];
			double it = double(TimingWindow[TC_Loop]) / double(frames) / 268100.0;
			double gc = double(TimingWindow[TC_GC]) / double(frames) / 268100.0;
			double net = double(TimingWindow[TC_Net]) / double(frames) / 268100.0;
			double wait = double(TimingWindow[TC_Wait]) / double(frames) / 268100.0;
			double avg = double(now - windowStart) / double(frames);
			double lo = double(TimingWindow[TC_Logic]) / double(frames) / 268100.0;
			double sc = double(TimingWindow[TC_Scene3D]) / double(frames) / 268100.0;
			double hdh = double(TimingWindow[TC_HDHud]) / double(frames) / 268100.0;
			double hdg = double(TimingWindow[TC_HDGPU]) / double(frames) / 268100.0;
			double inp = double(TimingWindow[TC_Input]) / double(frames) / 268100.0;
			double snd = double(TimingWindow[TC_Sound]) / double(frames) / 268100.0;
			double pre = double(TimingWindow[TC_PreDisplay]) / double(frames) / 268100.0;
			double fe = double(TimingWindow[TC_FrameEnd]) / double(frames) / 268100.0;
			snprintf(buf, sizeof(buf), "[FPS] T+%lld frames=%d avg=%.1fms (%.1f fps) worst=%dms draws/frame=%d gs=%d menu=%d lo=%.1f/%.1f sc=%.1f/%.1f hdH=%.1f/%.1f hdG=%.1f/%.1f gu=%.1f/%.1f tx=%d/%.1f/%.1f mu=%.1f/%.1f ti=%.2f gr=%.1f/%.1f pr=%.1f/%.1f in=%.1f/%.1f snd=%.1f/%.1f pre=%.1f/%.1f fe=%.1f/%.1f it=%.1f gc=%.1f net=%.1f wait=%.1f d2=%d\n",
				now, frames, avg, frames * 1000.0 / double(now - windowStart),
				worst, draws / frames, int(gamestate), int(menuactive),
				lo,
				double(TimingWorst[TC_Logic]) / 268100.0,
				sc,
				double(TimingWorst[TC_Scene3D]) / 268100.0,
				hdh,
				double(TimingWorst[TC_HDHud]) / 268100.0,
				hdg,
				double(TimingWorst[TC_HDGPU]) / 268100.0,
				double(TimingWindow[TC_GPUWait]) / double(frames) / 268100.0,
				double(TimingWorst[TC_GPUWait]) / 268100.0,
				TimingTexWindow,
				double(TimingWindow[TC_Texture]) / double(frames) / 268100.0,
				double(TimingWorst[TC_Texture]) / 268100.0,
				double(TimingWindow[TC_Music]) / double(frames) / 268100.0,
				double(TimingWorst[TC_Music]) / 268100.0,
				double(TimingTicsWindow) / double(frames),
				double(TimingGPUWindow) / double(frames),
				TimingGPUWindow,
				double(TimingCPUWindow) / double(frames),
				TimingCPUWindow,
				inp,
				double(TimingWorst[TC_Input]) / 268100.0,
				snd,
				double(TimingWorst[TC_Sound]) / 268100.0,
				pre,
				double(TimingWorst[TC_PreDisplay]) / 268100.0,
				fe,
				double(TimingWorst[TC_FrameEnd]) / 268100.0,
				it, gc, net, wait,
				Timing2DDrawsWindow);
			N3DS_Log(buf);
			N3DS_LogFlush();
			// Reset window accumulators
			for (int i = 0; i < TC_COUNT; i++) { TimingWindow[i] = 0; TimingWorst[i] = 0; }
			TimingTexWindow = 0;
			TimingGPUWindow = 0;
			TimingCPUWindow = 0;
			TimingTicsWindow = 0;
			Timing2DDrawsWindow = 0;
			windowStart = now;
			frames = draws = worst = 0;
		}
	}
	Frame++;
	Arrays = Elements = NotSent = 0;
	BeginRetries = 0;
	// Reset per-frame timing: promote frame totals to worst-case, then clear
	for (int i = 0; i < TC_COUNT; i++) {
		if (TimingFrame[i] > TimingWorst[i]) TimingWorst[i] = TimingFrame[i];
		TimingFrame[i] = 0;
	}
}

void TimingBegin(int category)
{
	if (category < 0 || category >= TC_COUNT) return;
	TimingBeginTicks[category] = svcGetSystemTick();
}

void TimingEnd(int category)
{
	if (category < 0 || category >= TC_COUNT) return;
	if (TimingBeginTicks[category] > 0)
	{
		unsigned long long delta = svcGetSystemTick() - TimingBeginTicks[category];
		TimingFrame[category] += delta;
		TimingWindow[category] += delta;
		TimingBeginTicks[category] = 0;
	}
	if (category == TC_Texture)
	{
		TimingTexWindow++;
	}
}

void Draw(bool indexed, bool sent)
{
	if (!sent) NotSent++;
	else if (indexed) Elements++;
	else Arrays++;
}

bool WantDrawDetails()
{
	if (DrawsDescribed >= DetailDraws) return false;
	DrawsDescribed++;
	return true;
}

bool MenuDiagWantFrame()
{
	if (!MenuDiag) return false;
	static int prevMenuActive = -1;
	if (menuactive != MENU_Off && prevMenuActive == MENU_Off) {
		MenuDiagMenuFrames = 0;
	}
	prevMenuActive = menuactive;
	if (menuactive != MENU_Off && MenuDiagMenuFrames < 20) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[MENU] f%d state menu=%d gs=%d want=1\n", Frame, (int)menuactive, (int)gamestate);
		N3DS_Log(buf);
		N3DS_LogFlush();
		MenuDiagMenuFrames++;
		return true;
	}
	if (menuactive != MENU_Off) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[MENU] f%d state menu=%d gs=%d want=0\n", Frame, (int)menuactive, (int)gamestate);
		N3DS_Log(buf);
		N3DS_LogFlush();
	}
	if (gamestate == GS_LEVEL && menuactive == MENU_Off && MenuDiagHudFrames < 3) {
		MenuDiagHudFrames++;
		return true;
	}
	return false;
}

static int MenuDiagSciN = 0;
static int MenuDiagSciMinL, MenuDiagSciMinT, MenuDiagSciMaxR, MenuDiagSciMaxB;
static int MenuDiagSciFrame = -1;

void MenuDiagRecordScissor(int l, int t, int r, int b)
{
	if (!MenuDiag) return;
	int fn = Frame;
	if (fn != MenuDiagSciFrame) {
		MenuDiagSciFrame = fn;
		MenuDiagSciN = 0;
		MenuDiagSciMinL = 9999;
		MenuDiagSciMinT = 9999;
		MenuDiagSciMaxR = -9999;
		MenuDiagSciMaxB = -9999;
	}
	MenuDiagSciN++;
	if (l < MenuDiagSciMinL) MenuDiagSciMinL = l;
	if (t < MenuDiagSciMinT) MenuDiagSciMinT = t;
	if (r > MenuDiagSciMaxR) MenuDiagSciMaxR = r;
	if (b > MenuDiagSciMaxB) MenuDiagSciMaxB = b;
}

bool MenuDiagEmitScissor()
{
	if (!MenuDiag || MenuDiagSciN == 0) return false;
	char buf[128];
	snprintf(buf, sizeof(buf), "[MENU] f%d sci n=%d min=%d,%d max=%d,%d\n",
		Frame, MenuDiagSciN, MenuDiagSciMinL, MenuDiagSciMinT, MenuDiagSciMaxR, MenuDiagSciMaxB);
	N3DS_Log(buf);
	N3DS_LogFlush();
	MenuDiagSciN = 0;
	return true;
}

}
}
