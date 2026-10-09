/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Errors.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>


// One row per Errors.h constant; check_errors_table.sh enforces it.
struct vos_error_entry {
	int code;
	const char* text;
};


static const vos_error_entry kVosErrorTable[] = {
	{ B_OK, "Success" },
	{ B_NO_ERROR, "Success" },
	{ B_ERROR, "General error" },

	{ B_BAD_INDEX, "Bad index" },
	{ B_BAD_TYPE, "Bad type" },
	{ B_BAD_VALUE, "Bad value" },
	{ B_MISMATCHED_VALUES, "Mismatched values" },
	{ B_NAME_NOT_FOUND, "Name not found" },
	{ B_NAME_IN_USE, "Name in use" },
	{ B_NOT_INITIALIZED, "Not initialized" },
	{ B_NOT_ALLOWED, "Not allowed" },
	{ B_DONT_DO_THAT, "Don't do that" },

	{ B_NO_MEMORY, "Out of memory" },
	{ B_IO_ERROR, "Input/output error" },
	{ B_PERMISSION_DENIED, "Permission denied" },
	{ B_TIMED_OUT, "Operation timed out" },
	{ B_INTERRUPTED, "Interrupted" },
	{ B_WOULD_BLOCK, "Operation would block" },
	{ B_CANCELED, "Canceled" },
	{ B_NO_INIT, "No such device or address" },
	{ B_BUSY, "Device or resource busy" },
	{ B_BAD_DATA, "Illegal byte sequence" },

	{ B_BAD_SEM_ID, "Bad semaphore ID" },
	{ B_NO_MORE_SEMS, "No locks available" },
	{ B_BAD_THREAD_ID, "Bad thread ID" },
	{ B_NO_MORE_THREADS, "Out of threads" },
	{ B_BAD_THREAD_STATE, "Bad thread state" },
	{ B_BAD_TEAM_ID, "No such process" },
	{ B_NO_MORE_TEAMS, "Out of teams" },
	{ B_BAD_PORT_ID, "Bad port ID" },
	{ B_NO_MORE_PORTS, "Too many open files in system" },
	{ B_BAD_IMAGE_ID, "Bad image ID" },
	{ B_BAD_ADDRESS, "Bad address" },
	{ B_NOT_AN_EXECUTABLE, "Exec format error" },
	{ B_MISSING_LIBRARY, "Missing library" },
	{ B_MISSING_SYMBOL, "Missing symbol" },
	{ B_UNKNOWN_EXECUTABLE, "Unknown executable" },
	{ B_LEGACY_EXECUTABLE, "Legacy executable" },
	{ B_DEBUGGER_ALREADY_INSTALLED, "Debugger already installed" },
	{ B_BAD_REPLY, "Bad message" },
	{ B_DUPLICATE_REPLY, "Duplicate reply" },
	{ B_MESSAGE_TO_SELF, "Message to self" },
	{ B_BAD_HANDLER, "Bad handler" },
	{ B_ALREADY_RUNNING, "Operation already in progress" },
	{ B_LAUNCH_FAILED, "Launch failed" },
	{ B_AMBIGUOUS_APP_LAUNCH, "Ambiguous application launch" },
	{ B_UNKNOWN_MIME_TYPE, "Unknown MIME type" },
	{ B_BAD_SCRIPT_SYNTAX, "Bad script syntax" },
	{ B_LAUNCH_FAILED_NO_RESOLVE_LINK, "Launch failed: could not resolve link" },
	{ B_LAUNCH_FAILED_EXECUTABLE, "Launch failed: executable" },
	{ B_LAUNCH_FAILED_APP_NOT_FOUND, "Launch failed: application not found" },
	{ B_LAUNCH_FAILED_APP_IN_TRASH, "Launch failed: application in trash" },
	{ B_LAUNCH_FAILED_NO_PREFERRED_APP, "Launch failed: no preferred application" },
	{ B_LAUNCH_FAILED_FILES_APP_NOT_FOUND, "Launch failed: files application not found" },
	{ B_BAD_MIME_SNIFFER_RULE, "Bad MIME sniffer rule" },
	{ B_NOT_A_MESSAGE, "Not a message" },
	{ B_SHUTDOWN_CANCELLED, "Shutdown cancelled" },
	{ B_SHUTTING_DOWN, "Shutting down" },

	{ B_FILE_ERROR, "Bad file descriptor" },
	{ B_FILE_NOT_FOUND, "File not found" },
	{ B_FILE_EXISTS, "File exists" },
	{ B_ENTRY_NOT_FOUND, "Entry not found" },
	{ B_NAME_TOO_LONG, "Name too long" },
	{ B_NOT_A_DIRECTORY, "Not a directory" },
	{ B_DIRECTORY_NOT_EMPTY, "Directory not empty" },
	{ B_DEVICE_FULL, "No space left on device" },
	{ B_READ_ONLY_DEVICE, "Read-only file system" },
	{ B_IS_A_DIRECTORY, "Is a directory" },
	{ B_NO_MORE_FDS, "Too many open files" },
	{ B_CROSS_DEVICE_LINK, "Cross-device link" },
	{ B_LINK_LIMIT, "Too many levels of symbolic links" },
	{ B_BUSTED_PIPE, "Broken pipe" },
	{ B_UNSUPPORTED, "Operation not supported" },
	{ B_PARTITION_TOO_SMALL, "Partition too small" },
	{ B_PARTIAL_READ, "Partial read" },
	{ B_PARTIAL_WRITE, "Partial write" },
	{ B_BUFFER_OVERFLOW, "Value too large for defined data type" },
	{ B_TOO_MANY_ARGS, "Argument list too long" },
	{ B_FILE_TOO_LARGE, "File too large" },
	{ B_RESULT_NOT_REPRESENTABLE, "Numerical result out of range" },
	{ B_DEVICE_NOT_FOUND, "No such device" },
	{ B_NOT_SUPPORTED, "Operation not supported" },

	{ B_STREAM_NOT_FOUND, "Stream not found" },
	{ B_SERVER_NOT_FOUND, "Server not found" },
	{ B_RESOURCE_NOT_FOUND, "Resource not found" },
	{ B_RESOURCE_UNAVAILABLE, "Resource unavailable" },
	{ B_BAD_SUBSCRIBER, "Bad subscriber" },
	{ B_SUBSCRIBER_NOT_ENTERED, "Subscriber not entered" },
	{ B_BUFFER_NOT_AVAILABLE, "Buffer not available" },
	{ B_LAST_BUFFER_ERROR, "Last buffer error" },
	{ B_MEDIA_SYSTEM_FAILURE, "Media system failure" },
	{ B_MEDIA_BAD_NODE, "Bad media node" },
	{ B_MEDIA_NODE_BUSY, "Media node busy" },
	{ B_MEDIA_BAD_FORMAT, "Bad media format" },
	{ B_MEDIA_BAD_BUFFER, "Bad media buffer" },
	{ B_MEDIA_TOO_MANY_NODES, "Too many media nodes" },
	{ B_MEDIA_TOO_MANY_BUFFERS, "Too many media buffers" },
	{ B_MEDIA_NODE_ALREADY_EXISTS, "Media node already exists" },
	{ B_MEDIA_BUFFER_ALREADY_EXISTS, "Media buffer already exists" },
	{ B_MEDIA_CANNOT_SEEK, "Media cannot seek" },
	{ B_MEDIA_CANNOT_CHANGE_RUN_MODE, "Media cannot change run mode" },
	{ B_MEDIA_APP_ALREADY_REGISTERED, "Media app already registered" },
	{ B_MEDIA_APP_NOT_REGISTERED, "Media app not registered" },
	{ B_MEDIA_CANNOT_RECLAIM_BUFFERS, "Media cannot reclaim buffers" },
	{ B_MEDIA_BUFFERS_NOT_RECLAIMED, "Media buffers not reclaimed" },
	{ B_MEDIA_TIME_SOURCE_STOPPED, "Media time source stopped" },
	{ B_MEDIA_TIME_SOURCE_BUSY, "Media time source busy" },
	{ B_MEDIA_BAD_SOURCE, "Bad media source" },
	{ B_MEDIA_BAD_DESTINATION, "Bad media destination" },
	{ B_MEDIA_ALREADY_CONNECTED, "Media already connected" },
	{ B_MEDIA_NOT_CONNECTED, "Media not connected" },
	{ B_MEDIA_BAD_CLIP_FORMAT, "Bad media clip format" },
	{ B_MEDIA_ADDON_FAILED, "Media add-on failed" },
	{ B_MEDIA_ADDON_DISABLED, "Media add-on disabled" },
	{ B_MEDIA_CHANGE_IN_PROGRESS, "Media change in progress" },
	{ B_MEDIA_STALE_CHANGE_COUNT, "Media stale change count" },
	{ B_MEDIA_ADDON_RESTRICTED, "Media add-on restricted" },
	{ B_MEDIA_NO_HANDLER, "Media handler not found" },
	{ B_MEDIA_DUPLICATE_FORMAT, "Duplicate media format" },
	{ B_MEDIA_REALTIME_DISABLED, "Media realtime disabled" },
	{ B_MEDIA_REALTIME_UNAVAILABLE, "Media realtime unavailable" },

	{ B_MAIL_NO_DAEMON, "Mail daemon not running" },
	{ B_MAIL_UNKNOWN_USER, "Mail unknown user" },
	{ B_MAIL_WRONG_PASSWORD, "Mail wrong password" },
	{ B_MAIL_UNKNOWN_HOST, "Mail unknown host" },
	{ B_MAIL_ACCESS_ERROR, "Mail access error" },
	{ B_MAIL_UNKNOWN_FIELD, "Mail unknown field" },
	{ B_MAIL_NO_RECIPIENT, "Mail no recipient" },
	{ B_MAIL_INVALID_MAIL, "Invalid mail" },

	{ B_NO_PRINT_SERVER, "No print server" },

	{ B_DEV_INVALID_IOCTL, "Inappropriate ioctl for device" },
	{ B_DEV_NO_MEMORY, "Device out of memory" },
	{ B_DEV_BAD_DRIVE_NUM, "Bad drive number" },
	{ B_DEV_NO_MEDIA, "No medium found" },
	{ B_DEV_UNREADABLE, "Device unreadable" },
	{ B_DEV_FORMAT_ERROR, "Device format error" },
	{ B_DEV_TIMEOUT, "Device timeout" },
	{ B_DEV_RECALIBRATE_ERROR, "Device recalibrate error" },
	{ B_DEV_SEEK_ERROR, "Illegal seek" },
	{ B_DEV_ID_ERROR, "Device ID error" },
	{ B_DEV_READ_ERROR, "Device read error" },
	{ B_DEV_WRITE_ERROR, "Device write error" },
	{ B_DEV_NOT_READY, "Device not ready" },
	{ B_DEV_MEDIA_CHANGED, "Device media changed" },
	{ B_DEV_MEDIA_CHANGE_REQUESTED, "Device media change requested" },
	{ B_DEV_RESOURCE_CONFLICT, "Device resource conflict" },
	{ B_DEV_CONFIGURATION_ERROR, "Device configuration error" },
	{ B_DEV_DISABLED_BY_USER, "Device disabled by user" },
	{ B_DEV_DOOR_OPEN, "Device door open" },
	{ B_DEV_INVALID_PIPE, "Device invalid pipe" },
	{ B_DEV_CRC_ERROR, "Device CRC error" },
	{ B_DEV_STALLED, "Device stalled" },
	{ B_DEV_BAD_PID, "Device bad PID" },
	{ B_DEV_UNEXPECTED_PID, "Device unexpected PID" },
	{ B_DEV_DATA_OVERRUN, "Device data overrun" },
	{ B_DEV_DATA_UNDERRUN, "No data available" },
	{ B_DEV_FIFO_OVERRUN, "Device FIFO overrun" },
	{ B_DEV_FIFO_UNDERRUN, "Device FIFO underrun" },
	{ B_DEV_PENDING, "Operation in progress" },
	{ B_DEV_MULTIPLE_ERRORS, "Device multiple errors" },
	{ B_DEV_TOO_LATE, "Device too late" },

	{ B_TRANSLATION_BASE_ERROR, "Translation base error" },
	{ B_NO_TRANSLATOR, "No translator" },
	{ B_ILLEGAL_DATA, "Illegal data" },

	{ B_GENERAL_ERROR_BASE, "General error base" },
	{ B_OS_ERROR_BASE, "Operating system error base" },
	{ B_APP_ERROR_BASE, "Application error base" },
	{ B_INTERFACE_ERROR_BASE, "Interface error base" },
	{ B_MEDIA_ERROR_BASE, "Media error base" },
	{ B_TRANSLATION_ERROR_BASE, "Translation error base" },
	{ B_MIDI_ERROR_BASE, "MIDI error base" },
	{ B_STORAGE_ERROR_BASE, "Storage error base" },
	{ B_POSIX_ERROR_BASE, "POSIX error base" },
	{ B_MAIL_ERROR_BASE, "Mail error base" },
	{ B_PRINT_ERROR_BASE, "Print error base" },
	{ B_DEVICE_ERROR_BASE, "Device error base" },
	{ B_ERRORS_END, "Errors end" },
};


static __thread char sVosUnknownErrorBuffer[64];


static const char*
vos_unknown_error_text(int errnum)
{
	snprintf(sVosUnknownErrorBuffer, sizeof(sVosUnknownErrorBuffer),
		"Unknown error %d", errnum);
	return sVosUnknownErrorBuffer;
}


// glibc 2.32+; weak so an older libc still links.
extern "C" const char* strerrordesc_np(int errnum) __attribute__((weak));


static const char*
vos_errno_text(int errnum)
{
	if (strerrordesc_np != NULL) {
		const char* text = strerrordesc_np(errnum);
		if (text != NULL)
			return text;
	}
	return NULL;
}


const char*
vos_status_strerror(int errnum)
{
	if (errnum > 0) {
		const char* text = vos_errno_text(errnum);
		return text != NULL ? text : vos_unknown_error_text(errnum);
	}

	// Table first so B_ENTRY_NOT_FOUND and friends keep V\OS wording
	// even when the value is also a well-known -errno.
	for (size_t i = 0;
		i < sizeof(kVosErrorTable) / sizeof(kVosErrorTable[0]); i++) {
		if (kVosErrorTable[i].code == errnum)
			return kVosErrorTable[i].text;
	}

	if (errnum < 0) {
		int positive = -errnum;
		// Native codes sit near INT_MIN; only small negatives are -errno.
		if (positive > 0 && positive < 4096) {
			const char* text = vos_errno_text(positive);
			if (text != NULL)
				return text;
		}
	}

	return vos_unknown_error_text(errnum);
}


extern "C" char*
strerror(int errnum)
{
	return const_cast<char*>(vos_status_strerror(errnum));
}


// GNU flavour: may return a static string instead of buffer.
extern "C" char*
strerror_r(int errnum, char* buffer, size_t bufferSize)
{
	// glibc declares buffer nonnull; only the size can be zero.
	if (bufferSize == 0)
		return NULL;

	const char* text = vos_status_strerror(errnum);
	if (text != sVosUnknownErrorBuffer)
		return const_cast<char*>(text);

	snprintf(buffer, bufferSize, "%s", text);
	return buffer;
}


// XSI flavour, used when callers compile without _GNU_SOURCE.
extern "C" int
__xpg_strerror_r(int errnum, char* buffer, size_t bufferSize)
{
	if (bufferSize == 0)
		return EINVAL;

	strerror_r(errnum, buffer, bufferSize);
	return 0;
}


extern "C" void
perror(const char* s)
{
	int errnum = errno;
	if (s != NULL && s[0] != '\0')
		fprintf(stderr, "%s: %s\n", s, strerror(errnum));
	else
		fprintf(stderr, "%s\n", strerror(errnum));
}
