/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>


static int		sLogLevel  = 2;		// default: info
static FILE*	sLogFile   = nullptr;
static std::mutex sLogMutex;


void
log_init_from_env()
{
	const char* lvl = getenv("VOS_LOG_LEVEL");
	if (lvl) {
		if (strcmp(lvl, "error") == 0)      sLogLevel = 0;
		else if (strcmp(lvl, "warn") == 0)  sLogLevel = 1;
		else if (strcmp(lvl, "info") == 0)  sLogLevel = 2;
		else if (strcmp(lvl, "debug") == 0) sLogLevel = 3;
	}
}


void
log_set_file(const char* path)
{
	std::lock_guard<std::mutex> lock(sLogMutex);
	if (sLogFile && sLogFile != stderr)
		fclose(sLogFile);
	sLogFile = fopen(path, "a");
	if (!sLogFile) {
		sLogFile = nullptr;
		fprintf(stderr, "[vos:ERR] failed to open log file: %s\n", path);
	}
}


static void
vlog(const char* prefix, const char* fmt, va_list ap)
{
	FILE* out = sLogFile ? sLogFile : stderr;
	std::lock_guard<std::mutex> lock(sLogMutex);
	fprintf(out, "%s", prefix);
	vfprintf(out, fmt, ap);
	fprintf(out, "\n");
	fflush(out);
}


void
log_error(const char* fmt, ...)
{
	if (sLogLevel < 0) return;
	va_list ap;
	va_start(ap, fmt);
	vlog("[vos:ERR] ", fmt, ap);
	va_end(ap);
}


void
log_warn(const char* fmt, ...)
{
	if (sLogLevel < 1) return;
	va_list ap;
	va_start(ap, fmt);
	vlog("[vos:WRN] ", fmt, ap);
	va_end(ap);
}


void
log_info(const char* fmt, ...)
{
	if (sLogLevel < 2) return;
	va_list ap;
	va_start(ap, fmt);
	vlog("[vos:INF] ", fmt, ap);
	va_end(ap);
}


void
log_debug(const char* fmt, ...)
{
	if (sLogLevel < 3) return;
	va_list ap;
	va_start(ap, fmt);
	vlog("[vos:DBG] ", fmt, ap);
	va_end(ap);
}
