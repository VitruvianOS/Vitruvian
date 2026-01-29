/*
 * Copyright 2026, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */

#include <debugger.h>
#include <OS.h>

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>


bool _rtDebugFlag = true;


namespace BKernelPrivate {


#ifdef HAVE_LIBBACKTRACE
#include <backtrace.h>
static struct backtrace_state* sBacktraceState = NULL;
#endif


static void
PrintStackTrace()
{
#ifdef HAVE_LIBBACKTRACE
	if (sBacktraceState == NULL) {
		sBacktraceState = backtrace_create_state(NULL, 1, NULL, NULL);
	}

	if (sBacktraceState) {
		fprintf(stderr, "Stack trace:\n");
		int frame = 0;
		backtrace_full(sBacktraceState, 1,
			[](void* data, uintptr_t pc, const char* filename,
			   int lineno, const char* function) -> int {
				int* f = (int*)data;
				if (function) {
					int status;
					char* demangled = abi::__cxa_demangle(function, NULL, NULL, &status);
					fprintf(stderr, "  #%d  %s", (*f)++,
						demangled ? demangled : function);
					free(demangled);
					if (filename)
						fprintf(stderr, " at %s:%d", filename, lineno);
					fprintf(stderr, "\n");
				} else {
					fprintf(stderr, "  #%d  0x%lx\n", (*f)++, (unsigned long)pc);
				}
				return 0;
			},
			NULL, &frame);
		return;
	}
#endif

	fprintf(stderr, "WARNING: Fallback to glibc backtrace!");

	void* buffer[64];
	int nframes = backtrace(buffer, 64);
	char** symbols = backtrace_symbols(buffer, nframes);

	fprintf(stderr, "Stack trace:\n");
	for (int i = 1; i < nframes; i++) {
		if (symbols) {
			char* sym = symbols[i];
			char* begin = strchr(sym, '(');
			char* end = begin ? strchr(begin, '+') : NULL;

			if (begin && end) {
				*end = '\0';
				int status;
				char* demangled = abi::__cxa_demangle(begin + 1, NULL, NULL, &status);
				if (demangled) {
					*end = '+';
					fprintf(stderr, "  #%d  %s\n", i - 1, demangled);
					free(demangled);
					continue;
				}
				*end = '+';
			}
			fprintf(stderr, "  #%d  %s\n", i - 1, sym);
		} else {
			fprintf(stderr, "  #%d  %p\n", i - 1, buffer[i]);
		}
	}

	free(symbols);
}


} // namespace BKernelPrivate


void
debugger(const char* message)
{
	thread_id thread = find_thread(NULL);

	fprintf(stderr, "\n");
	fprintf(stderr, "=========================================\n");
	fprintf(stderr, "======== *** Guru Meditation *** ========\n");
	fprintf(stderr, "DEBUGGER: %s\n", message ? message : "(no message)");
	// TODO: get thread name
	fprintf(stderr, "Thread: %" B_PRId32 " (%s)\n", thread, "main");
	fprintf(stderr, "PID: %d\n", getpid());
	fprintf(stderr, "=========================================\n");
	BKernelPrivate::PrintStackTrace();
	fprintf(stderr, "=========================================\n");

	const char* action = getenv("VOS_DEBUGGER_ACTION");

	if (action && strcmp(action, "gdb") == 0) {
		fprintf(stderr, "Launching GDB...\n");

		char pid_str[16];
		snprintf(pid_str, sizeof(pid_str), "%d", getpid());

		pid_t child = fork();
		if (child == 0) {
			execlp("gdb", "gdb", "-p", pid_str, NULL);
			_exit(1);
		} else if (child > 0) {
			pause();
		}
	} else if (action && strcmp(action, "wait") == 0) {
		fprintf(stderr, "Waiting for debugger. Attach with:\n");
		fprintf(stderr, "  gdb -p %d\n", getpid());
		pause();
	} else if (action && strcmp(action, "core") == 0) {
		fprintf(stderr, "Generating core dump...\n");
		abort();
	} else {
		fprintf(stderr, "Exiting. Set VOS_DEBUGGER_ACTION=gdb|wait|core for debugging.\n");
		exit(-1);
	}
}


extern "C" {


int
_debuggerAssert(const char* file, int line, const char* message)
{
	char buffer[1024];
	const char *msg = (message && *message) ? message : "(no message)";
	const char *fname = file ? file : "(unknown)";

	int needed = snprintf(buffer, sizeof(buffer),
		"ASSERT FAILED: %s\n  File: %s\n  Line: %d",
		msg, fname, line);

	if (needed < 0) {
		debugger("ASSERT FAILED: <format error>");
	} else
		debugger(buffer);

	return 0;
}


status_t
debug_thread(thread_id thread)
{
	fprintf(stderr, "debug_thread(%" B_PRId32 "): Not implemented\n", thread);
	return B_UNSUPPORTED;
}


void
debug_printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	debug_vprintf(format, ap);
	va_end(ap);
}

void
debug_vprintf(const char *format, va_list args)
{
	char buffer[1024];
	va_list ap;

	va_copy(ap, args);
	int needed = vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	if (needed < 0) {
		fputs("<debug_vprintf format error>\n", stdout);
		return;
	}

	size_t to_write = (size_t)(needed < (int)sizeof(buffer)
		? needed : (int)sizeof(buffer) - 1);
   
	if (to_write > 0)
		fwrite(buffer, 1, to_write, stdout);
}


int
_debugPrintf(const char *fmt, ...)
{
	if (!_rtDebugFlag)
		return 0;

	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vfprintf(stdout, fmt, ap);
	va_end(ap);

	return ret;
}


int
_sPrintf(const char *fmt, ...)
{
	if (!_rtDebugFlag)
		return 0;

	char buffer[512];
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	if (ret < 0)
		return ret;

	size_t len = (size_t)(ret < (int)sizeof(buffer)
		? ret : sizeof(buffer) - 1);

	if (len == 0 || buffer[len - 1] != '\n') {
		if (len > 0)
			fwrite(buffer, 1, len, stdout);
		putchar('\n');
	} else
		fwrite(buffer, 1, len, stdout);

	return ret;
}


status_t
_kern_kernel_debugger(const char* message)
{
	debugger(message);
	return B_OK;
}


} // extern "C"
