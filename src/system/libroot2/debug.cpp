/*
 * Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <Debug.h>

#include <backward.hpp>


extern "C" {


void
debugger(const char* message)
{
	// TODO set handlers for default abort signals
	using namespace backward;
	StackTrace st; st.load_here(32);
	Printer p; p.print(st);

	abort();
}


status_t
_kern_kernel_debugger(const char* message)
{
	debugger(message);
}


void
debug_printf(const char* format, ...)
{
	va_list list;
	va_start(list, format);

	debug_vprintf(format, list);

	va_end(list);
}


void
debug_vprintf(const char* format, va_list args)
{
	char buffer[2048];
	vsnprintf(buffer, sizeof(buffer), format, args);

	printf(buffer);
}


status_t
debug_thread(thread_id thread)
{
	// TODO
	// FindThread::Debug()
}


}
