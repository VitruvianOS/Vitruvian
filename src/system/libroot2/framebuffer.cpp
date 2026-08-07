/*
 * Copyright 2018-2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>
#include <safemode_defs.h>
#include <driver_settings.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "syscalls.h"


status_t
_kern_frame_buffer_update(addr_t baseAddress, int32 width,
	int32 height, int32 depth, int32 bytesPerRow)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


// Safemode options map to kernel cmdline tokens; parses /proc/cmdline once
// and answers per-option queries.

static char sCmdLine[4096];
static bool sCmdLineLoaded = false;
static size_t sCmdLineLen = 0;


static void
load_cmdline()
{
	if (sCmdLineLoaded)
		return;
	sCmdLineLoaded = true;
	int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	ssize_t n = read(fd, sCmdLine, sizeof(sCmdLine) - 1);
	close(fd);
	if (n < 0)
		return;
	// Trim the trailing newline that /proc/cmdline appends.
	while (n > 0 && (sCmdLine[n - 1] == '\n' || sCmdLine[n - 1] == ' '))
		n--;
	sCmdLine[n] = '\0';
	sCmdLineLen = (size_t)n;
}


static bool
cmdline_has(const char* token)
{
	if (!sCmdLineLoaded)
		return false;
	size_t tokenLen = strlen(token);
	const char* p = sCmdLine;
	while (*p != '\0') {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		const char* word = p;
		while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		if ((size_t)(p - word) == tokenLen
			&& strncmp(word, token, tokenLen) == 0)
			return true;
	}
	return false;
}


status_t
_kern_get_safemode_option(const char *parameter,
	char *buffer, size_t *_bufferSize)
{
	load_cmdline();

	bool active = false;

	// safemode_defs.h constants → Linux cmdline tokens.
	if (strcmp(parameter, B_SAFEMODE_DISABLE_IDE_DMA) == 0)
		active = cmdline_has("libata.dma=0");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_ACPI) == 0)
		active = cmdline_has("acpi=off");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_APIC) == 0)
		active = cmdline_has("nolapic");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_IOAPIC) == 0)
		active = cmdline_has("noapic");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_X2APIC) == 0)
		active = cmdline_has("nox2apic");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_SMEP_SMAP) == 0)
		active = cmdline_has("nosmep") || cmdline_has("nosmap");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_APM) == 0)
		active = false;
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_SMP) == 0)
		active = cmdline_has("nosmp");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_PAT) == 0)
		active = cmdline_has("nopat");
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_HYPER_THREADING) == 0)
		active = cmdline_has("nosmt");
	else if (strcmp(parameter, B_SAFEMODE_FAIL_SAFE_VIDEO_MODE) == 0)
		active = cmdline_has("nomodeset");
	else if (strcmp(parameter, B_SAFEMODE_4_GB_MEMORY_LIMIT) == 0)
		active = cmdline_has("mem=4G");
	else if (strcmp(parameter, B_SAFEMODE_256_TB_MEMORY_LIMIT) == 0)
		active = false;
	else if (strcmp(parameter, B_SAFEMODE_DISABLE_USER_ADD_ONS) == 0)
		active = cmdline_has("vitruvian.disable_user_addons");
	else if (strcmp(parameter, B_SAFEMODE_SAFE_MODE) == 0)
		active = cmdline_has("vitruvian.safemode");
	else
		return B_NAME_NOT_FOUND;

	if (!active)
		return B_NAME_NOT_FOUND;

	// Consumers compare against "true"/"yes"/"on"/"enabled"/"1".
	static const char kValue[] = "true";
	const size_t valueLen = sizeof(kValue) - 1;
	if (*_bufferSize <= valueLen) {
		*_bufferSize = valueLen + 1;
		return B_BUFFER_OVERFLOW;
	}
	memcpy(buffer, kValue, valueLen);
	buffer[valueLen] = '\0';
	*_bufferSize = valueLen;
	return B_OK;
}
