#pragma once
#include <stdio.h>
#include <stdarg.h>

inline void debug_msg(const char *format, ...) {
#ifndef NDEBUG
	va_list arg;
	va_start(arg, format);
	vfprintf(stdout, format, arg);
	va_end(arg);
#endif
}