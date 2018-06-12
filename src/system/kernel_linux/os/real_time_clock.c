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
//	File Name:		real_time_clock.cpp
//	Author:			Bill Hayden (hayden@haydentech.com)
//	Description:	Implement clock functions.
//------------------------------------------------------------------------------

#include <OS.h>
#include <sys/time.h>

void
set_real_time_clock(uint32 currentTime)
{
	struct timeval tv;
	struct timezone tz;

	tv.tv_sec = (long)currentTime;
	tv.tv_usec = 0;
	tz.tz_minuteswest = 0;
	tz.tz_dsttime = 0;

	settimeofday(&tv, &tz);
}


uint32
real_time_clock(void)
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday( &tv, &tz ); /* timezone unused but can't pass NULL */

	return (bigtime_t)tv.tv_usec / (bigtime_t)(1000*1000) + (bigtime_t)tv.tv_sec;
}


bigtime_t
real_time_clock_usecs(void)
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday( &tv, &tz ); /* timezone unused but can't pass NULL */

	return (bigtime_t)tv.tv_sec * (bigtime_t)(1000*1000) + (bigtime_t)tv.tv_usec;
}
