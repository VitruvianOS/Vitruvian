/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */

#include "PartitionCapabilities.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Message.h>


static const char* kInstallHelper = "/usr/libexec/vos-install-helper";

static const char* kPkexec = "/usr/bin/pkexec";
static const char* kDriveSetupQuery = "/usr/libexec/vos-drivesetup-query";


static void
add_tool_list(const BMessage& result, const char* fieldName,
	BStringList& out)
{
	out.MakeEmpty();

	BString tool;
	for (int32 i = 0; result.FindString(fieldName, i, &tool) == B_OK; i++)
		out.Add(tool);
}


// Must mirror _partlib_kv_split() in vos-partition-lib.sh; same encoding.
static bool
split_kv(const BString& line, BString& key, BString& value)
{
	int32 eq = line.FindFirst('=');
	if (eq < 0)
		return false;
	line.CopyInto(key, 0, eq);
	value.SetTo(line.String() + eq + 1);
	return true;
}


static void
parse_capabilities(const BString& text, BMessage& out)
{
	int32 start = 0;
	int32 length = text.Length();
	while (start <= length) {
		int32 nl = text.FindFirst('\n', start);
		BString line;
		if (nl < 0) {
			if (start == length)
				break;
			text.CopyInto(line, start, length - start);
			start = length + 1;
		} else {
			text.CopyInto(line, start, nl - start);
			start = nl + 1;
		}

		if (line.IsEmpty() || line[0] == '#')
			continue;

		BString key, value;
		if (split_kv(line, key, value))
			out.AddString(key.String(), value);
	}
}


static status_t
run_helper(const char* execPath, const char* const argv[], BString& outText)
{
	int outPipe[2];
	if (pipe(outPipe) < 0)
		return B_ERROR;

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		return B_ERROR;
	}

	if (pid == 0) {
		close(outPipe[0]);
		dup2(outPipe[1], STDOUT_FILENO);
		close(outPipe[1]);
		execv(execPath, (char* const*)argv);
		_exit(127);
	}

	close(outPipe[1]);

	outText.Truncate(0);
	char buffer[512];
	ssize_t bytesRead;
	while ((bytesRead = read(outPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytesRead] = '\0';
		outText << buffer;
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return B_ERROR;

	return B_OK;
}


status_t
PartitionCapabilities::Get(const char* filesystem, PartitionCapabilities& out)
{
	if (filesystem == NULL || filesystem[0] == '\0')
		return B_BAD_VALUE;

	const char* argv[] = { "vos-install-helper", "partition",
		"capabilities", filesystem, NULL };
	BString text;
	if (run_helper(kInstallHelper, argv, text) != B_OK)
		return B_ERROR;

	BMessage result;
	parse_capabilities(text, result);

	result.FindString("filesystem", &out.filesystem);
	result.FindString("create", &out.create);
	result.FindString("grow", &out.grow);
	result.FindString("shrink", &out.shrink);
	result.FindString("move", &out.move);
	result.FindString("check", &out.check);
	result.FindString("read_label", &out.readLabel);
	result.FindString("write_label", &out.writeLabel);
	result.FindString("read_uuid", &out.readUuid);
	result.FindString("write_uuid", &out.writeUuid);

	BString flag;
	out.onlineGrow = (result.FindString("online_grow", &flag) == B_OK
		&& flag == "1");
	out.onlineShrink = (result.FindString("online_shrink", &flag) == B_OK
		&& flag == "1");

	add_tool_list(result, "tools_present", out.toolsPresent);
	add_tool_list(result, "tools_missing", out.toolsMissing);

	BString sizeStr;
	out.minSizeMiB = -1;
	out.maxSizeMiB = -1;
	if (result.FindString("minSizeMiB", &sizeStr) == B_OK && !sizeStr.IsEmpty())
		out.minSizeMiB = (off_t)atoll(sizeStr.String());
	if (result.FindString("maxSizeMiB", &sizeStr) == B_OK && !sizeStr.IsEmpty())
		out.maxSizeMiB = (off_t)atoll(sizeStr.String());

	return B_OK;
}


status_t
PartitionCapabilities::GetSizeLimits(const char* target,
	const char* filesystem, off_t& minSizeMiB, off_t& maxSizeMiB,
	off_t* usedMiB)
{
	minSizeMiB = -1;
	maxSizeMiB = -1;
	if (usedMiB != NULL)
		*usedMiB = -1;

	if (target == NULL || target[0] == '\0')
		return B_BAD_VALUE;

	// pkexec re-execs argv[1] as root, so the helper path must be argv[1].
	const char* argv[] = { "pkexec", kDriveSetupQuery, "partition",
		"size-limits", target,
		(filesystem != NULL && filesystem[0] != '\0') ? filesystem : NULL,
		NULL };
	BString text;
	if (run_helper(kPkexec, argv, text) != B_OK)
		return B_ERROR;

	BMessage result;
	parse_capabilities(text, result);

	BString sizeStr;
	if (result.FindString("minSizeMiB", &sizeStr) != B_OK || sizeStr.IsEmpty())
		return B_ERROR;
	minSizeMiB = (off_t)atoll(sizeStr.String());

	if (result.FindString("maxSizeMiB", &sizeStr) != B_OK || sizeStr.IsEmpty())
		return B_ERROR;
	maxSizeMiB = (off_t)atoll(sizeStr.String());

	if (usedMiB != NULL && result.FindString("usedMiB", &sizeStr) == B_OK
			&& !sizeStr.IsEmpty()) {
		*usedMiB = (off_t)atoll(sizeStr.String());
	}

	return B_OK;
}
