#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

inline void debug_msg(const char *format, ...) {
#ifndef NDEBUG
	va_list arg;
	va_start(arg, format);
	vfprintf(stdout, format, arg);
	va_end(arg);
#endif
}

typedef struct {
	size_t length, count;
	size_t byteSize;
	void *arr;
} EngineHeapArray;

#define ENGINE_HEAPARR(heapArr, type) ((type*)heapArr.arr)
#define ENGINE_HEAPARR_DEFAULT ((EngineHeapArray){.arr = NULL, .byteSize = 0, .length = 0, .count = 0})


void EngineCreateHeapArray(EngineHeapArray *heapArr);
void EngineDestroyHeapArray(EngineHeapArray *heapArr);
void EngineHeapArrayEnqueue(EngineHeapArray *heapArr, void *in);
void EngineHeapArrayDequeue(EngineHeapArray *heapArr, void *out);
#define EngineHeapArraypush(heapArr, in) EngineHeapArrayEnqueue(heapArr, in)
void EngineHeapArrayPop(EngineHeapArray *heapArr, void *out);