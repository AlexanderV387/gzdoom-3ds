/*
** platform_3ds.h
** The Nintendo 3DS port's platform services: its SD card folder, the log
** and fatal errors.
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#pragma once

// Everything the port reads and writes on the SD card: the IWAD, mods/,
// configuration, saves, screenshots, log.
#define N3DS_DATA_DIR "sdmc:/3ds/gzdoom"

void N3DS_Log(const char *text);			// appends to N3DS_DATA_DIR/log.txt
void N3DS_LogFlush();
void N3DS_LogMemory(const char *stage);
long long N3DS_ElapsedMs();			// milliseconds since program start
unsigned N3DS_HeapInUseKB();	// heap and linear memory, into the log

// Diagnostic: log heap state (in use, free, chunk count, trimmable) at a phase boundary.
void N3DS_LogHeapDiagnostic(const char *phase);
// Try to return unused memory at the heap's end via malloc_trim; logs before/after.
bool N3DS_TrimHeap();

[[noreturn]] void N3DS_FatalError(const char *message);
