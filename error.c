#include "csync2.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void csync_fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);

	exit(1);
}

void csync_debug(int lv, const char *fmt, ...)
{
	va_list ap;

	if ( csync_debug_level < lv ) return;

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);
}

