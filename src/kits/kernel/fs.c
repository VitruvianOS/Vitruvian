/*------------------------------------------------------------------------------
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
//	File Name:		fs.cpp
//	Authors:		Bill Hayden (hayden@haydentech.com)
//----------------------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>

#include <fs_attr.h>
#include <fs_info.h>

#include "../../config.h"

#if defined(COSMOE_ATTRIBUTES)
#include <sys/xattr.h>
#else
#warning Cosmoe does not support attributes on this platform
#endif


status_t _kstart_watching_vnode_(dev_t device, ino_t node,
								 uint32 flags, port_id port, int32 handlerToken);
status_t _kstop_watching_vnode_(dev_t device, ino_t node,
								port_id port, int32 handlerToken);
status_t _kstop_notifying_(port_id port, int32 handlerToken);



ssize_t  read_pos(int fd, off_t pos, void *buffer, size_t count)
{
	lseek(fd, pos, SEEK_SET);
	return read(fd, buffer, count);
}

ssize_t  write_pos(int fd, off_t pos, const void *buffer, size_t count)
{
	lseek(fd, pos, SEEK_SET);
	return write(fd, buffer, count);
}

dev_t	dev_for_path(const char *path)
{
	printf( "Cosmoe: UNIMPLEMENTED: dev_for_path\n" );
	return B_FILE_ERROR;
}

dev_t	next_dev(int32 *pos)
{
	printf( "Cosmoe: UNIMPLEMENTED: next_dev\n" );
	return B_BAD_VALUE;
}

int		fs_stat_dev(dev_t dev, fs_info *info)
{
	return -1;
}

ssize_t	fs_write_attr(int fd, const char *attribute, uint32 type, off_t pos, const void *buffer, size_t readBytes)
{
#if defined(COSMOE_ATTRIBUTES)
	return fsetxattr(fd, attribute, buffer, readBytes, 0);
#else
	printf( "Cosmoe: UNSUPPORTED: fsetxattr\n" );
	return (ssize_t)-1;
#endif
}

ssize_t	fs_read_attr(int fd, const char *attribute, uint32 type, off_t pos, void *buffer, size_t readBytes)
{
#if defined(COSMOE_ATTRIBUTES)
	return fgetxattr(fd, attribute, buffer, readBytes);
#else
	printf( "Cosmoe: UNSUPPORTED: fs_read_attr\n" );
	return (ssize_t)-1;
#endif
}

int		fs_remove_attr(int fd, const char *attribute)
{
#if defined(COSMOE_ATTRIBUTES)
	return fremovexattr(fd, attribute);
#else
	printf( "Cosmoe: UNSUPPORTED: fs_remove_attr\n" );
	return -1;
#endif
}

int		fs_stat_attr(int fd, const char *attribute, struct attr_info *attrInfo)
{
	printf( "Cosmoe: UNIMPLEMENTED: fs_stat_attr\n" );
	return -1;
}

status_t _kstart_watching_vnode_(dev_t device, ino_t node,
											uint32 flags, port_id port,
											int32 handlerToken)
{
	return B_ERROR;
}

/*!	\brief Unsubscribes a target from watching a node.
	\param device The device the node resides on (node_ref::device).
	\param node The node ID of the node (node_ref::device).
	\param port The port of the target (a looper port).
	\param handlerToken The token of the target handler. \c -2, if the
		   preferred handler of the looper is the target.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t _kstop_watching_vnode_(dev_t device, ino_t node,
										   port_id port, int32 handlerToken)
{
	return B_ERROR;
}


/*!	\brief Unsubscribes a target from node and mount monitoring.
	\param port The port of the target (a looper port).
	\param handlerToken The token of the target handler. \c -2, if the
		   preferred handler of the looper is the target.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t _kstop_notifying_(port_id port, int32 handlerToken)
{
	return B_ERROR;
}


