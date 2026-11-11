/*
** c3d_diag.cpp
** Diagnostics for the citro3d backend (see c3d_diag.h).
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "c3d_diag.h"
#include "platform_3ds.h"

namespace C3DRenderer
{
namespace Diag
{

bool NoDraw = false;
bool NoTex = false;
bool Triangle = false;
bool MinimalShader = false;

static const int SummaryFrames = 12;	// frames with a summary line
static const int DetailDraws = 8;		// draws described in full

static int Frame = 0;					// frames begun or attempted
static bool Hang = false;
static int BeginRetries = 0;
static bool BeginOk = false;
static int Arrays = 0, Elements = 0, NotSent = 0;
static int DrawsDescribed = 0;

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
	Line("switches: nodraw=%d notex=%d triangle=%d minshader=%d", NoDraw, NoTex, Triangle, MinimalShader);
}

bool HangReported()
{
	return Hang;
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
	if (Frame < SummaryFrames)
	{
		Line("frame %d: begin=%s retries=%d draws: arrays=%d elements=%d not_sent=%d%s",
			Frame, BeginOk ? "ok" : "FAILED", BeginRetries, Arrays, Elements, NotSent,
			drawn ? (Triangle ? " (test triangle)" : "") : " (skipped: no frame)");
	}
	Frame++;
	Arrays = Elements = NotSent = 0;
	BeginRetries = 0;
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

}
}
