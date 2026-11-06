#pragma once

// GZDoom 3DS (AlexanderV387, n3ds-ports), from tinaut1986/legend-of-doom-3ds.
// The SD folder of this fork. The upstream port
// uses sdmc:/3ds/legend-of-doom/; a folder of its own lets both apps be
// installed side by side without sharing saves, settings or updates.
#define LOD3DS_APP_SD_PATH "/3ds/gzdoom"
#define LOD3DS_APP_DIR "sdmc:" LOD3DS_APP_SD_PATH

// Title ID of this fork's CIA (platform/3ds/cia/legend-of-doom-3ds.rsf,
// UniqueId 0x5A2D1). Upstream's is 0x0004000005A2D000. The updater refuses a
// CIA with any other ID.
#define LOD3DS_TITLE_ID 0x000400000f3d5c00ull
