/*
** st_start_3ds.cpp
** The startup "screen" on the Nintendo 3DS: progress goes to the log only.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <algorithm>

#include "basics.h"
#include "st_start.h"


class FLogStartupScreen : public FStartupScreen
{
public:
	FLogStartupScreen(int max_progress) : FStartupScreen(max_progress) {}
	void Progress(int amount = 1) override { CurPos = std::min(CurPos + amount, MaxPos); }
};

FStartupScreen *FStartupScreen::CreateInstance(int max_progress)
{
	return new FLogStartupScreen(max_progress);
}
