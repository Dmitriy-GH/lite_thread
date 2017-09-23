// Объем занятой памяти в Кб

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <psapi.h>

size_t mem_used() {
	PROCESS_MEMORY_COUNTERS pmc;
	if(GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		return pmc.WorkingSetSize / 1024;
	} else {
		return 0;
	}
}

#else
#include <sys/time.h>
#include <sys/resource.h>

size_t mem_used() {
	struct rusage ru;
	if(getrusage(RUSAGE_SELF, &ru) == 0) {
		return ru.ru_maxrss;
	} else {
		return 0;
	}
}
#endif