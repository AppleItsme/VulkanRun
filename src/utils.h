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
} EngineQueue;


void EngineCreateQueue(EngineQueue *queue);
void EngineDestroyQueue(EngineQueue *queue);
void EngineQueueAdd(EngineQueue *queue, void *in);
void EngineQueueRetreive(EngineQueue *queue, void *out);
void EngineQueuePeek(EngineQueue *queue, void *out);