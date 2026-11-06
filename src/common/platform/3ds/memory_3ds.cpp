/*
** memory_3ds.cpp
** Memory diagnostics for the Nintendo 3DS port: how the application's memory
** is split (heap / linear), and every failed allocation with its size, in
** the log. The allocator calls are wrapped at link time (--wrap).
**
** 2026 AlexanderV387 (Nintendo 3DS port). GPL v3 (see LICENSE).
*/

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stddef.h>

#include "platform_3ds.h"

extern "C"
{
	extern u32 __ctru_heap_size, __ctru_linear_heap_size;

	void *__real_malloc(size_t size);
	void *__real_calloc(size_t n, size_t size);
	void *__real_realloc(void *ptr, size_t size);
	void *__real_memalign(size_t alignment, size_t size);

	static void LogFailure(const char *what, size_t size)
	{
		char line[160];
		struct mallinfo mi = mallinfo();
		snprintf(line, sizeof(line), "%s(%u) FAILED: heap %u KB, in use %u KB, free in heap %u KB\n",
			what, (unsigned)size, (unsigned)(__ctru_heap_size / 1024), (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024));
		N3DS_Log(line);
		N3DS_LogFlush();
	}

	void *__wrap_malloc(size_t size)
	{
		void *p = __real_malloc(size);
		if (p == nullptr && size) LogFailure("malloc", size);
		return p;
	}

	void *__wrap_calloc(size_t n, size_t size)
	{
		void *p = __real_calloc(n, size);
		if (p == nullptr && n && size) LogFailure("calloc", n * size);
		return p;
	}

	void *__wrap_realloc(void *ptr, size_t size)
	{
		void *p = __real_realloc(ptr, size);
		if (p == nullptr && size) LogFailure("realloc", size);
		return p;
	}

	void *__wrap_memalign(size_t alignment, size_t size)
	{
		void *p = __real_memalign(alignment, size);
		if (p == nullptr && size) LogFailure("memalign", size);
		return p;
	}
}

void N3DS_LogMemory(const char *stage)
{
	char line[200];
	struct mallinfo mi = mallinfo();
	snprintf(line, sizeof(line), "[mem] %s: app %u KB, heap %u KB (in use %u KB), linear %u KB (free %u KB)\n", stage,
		(unsigned)(osGetMemRegionSize(MEMREGION_APPLICATION) / 1024), (unsigned)(__ctru_heap_size / 1024),
		(unsigned)(mi.uordblks / 1024), (unsigned)(__ctru_linear_heap_size / 1024), (unsigned)(linearSpaceFree() / 1024));
	N3DS_Log(line);
	N3DS_LogFlush();
}
