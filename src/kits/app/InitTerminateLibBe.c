//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		InitTerminateLibBe.cpp
//	Author(s):		Ingo Weinhold (bonefish@users.sf.net)
//	Description:	Global library initialization/termination routines.
//------------------------------------------------------------------------------
#include <stdio.h>


// Reinstate the includes and change the filename back to .cpp 
// when gcc bug 16717 is fixed.  The c++ compiler in 3.4.1 and 3.5
// strips out these initialization functions.

#if 0
#include <MessagePrivate.h>
#include <RosterPrivate.h>
#else
extern int	_init_roster_();
extern int	_delete_roster_();
extern void	_msg_cache_cleanup_();
extern int	_init_message_();
extern int	_delete_message_();
#endif

// debugging
#define DBG(x) x
//#define DBG(x)
#define OUT	printf



// initialize_before
static void __attribute__ ((constructor))
initialize_before()
{
DBG(OUT("initialize_before()\n"));

	_init_message_();
	_init_roster_();

DBG(OUT("initialize_before() done\n"));
}

// terminate_after
static void __attribute__ ((destructor))
terminate_after()
{
DBG(OUT("terminate_after()\n"));

	_delete_roster_();
	_delete_message_();
	_msg_cache_cleanup_();

DBG(OUT("terminate_after() done\n"));
}

