//------------------------------------------------------------------------------
//	Copyright (c) 2004, Bill Hayden
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
//	File Name:		direntfd.c
//	Author:			Bill Hayden (hayden@haydentech.com)
//	Description:	dirent functionality using only an fd
//------------------------------------------------------------------------------


#include <dirent.h>

struct DIR_STRUCT
{
	int fd;
};


/* Open a directory stream on a fd.  */
DIR* opendirfd(int fd)
{
	DIR* dir = opendir("/");

	/*
	** Inject our fd into the private structure, now that the DIR*
	** is all set up for us.  If an OS does not have an int representing
	** the file descriptor as the first argument, things could get ugly
	*/
	if (dir)
		((DIR_STRUCT*)dir)->fd = fd;

	return dir;
}