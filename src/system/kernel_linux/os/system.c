/*
 ** Copyright (c) 2003, Tom Marshall (tommy@tig-grr.com). All rights reserved
 ** Distributed under the terms of the MIT License.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <errno.h>
#include <unistd.h>

#include <Debug.h>
#include <SupportDefs.h>

#include <LinuxBuildCompatibility.h>

#if defined(linux)
#include <sys/sysinfo.h>
#else
#warning System information not available on this platform
#warning system_time() will always return 0 on this platform
#endif

extern mode_t __gUmask = 022;


/* helper for get_system_info */
static void get_cpu_info( system_info* psInfo )
{
#if defined(linux)
	FILE*         fp;
	int           ncpu;
	char          buf[80];
	char*         p;
	bigtime_t     systime;
	bigtime_t     idletime;
	unsigned long n1, n2, n3, nidle;

	systime = system_time();
	psInfo->boot_time = real_time_clock_usecs() - systime;
	psInfo->bus_clock_speed = 66;	/* FIXME */
	ncpu = 0;
	if( (fp = fopen( "/proc/cpuinfo", "r" )) != NULL )
	{
		while( fgets( buf, sizeof(buf), fp ) != NULL )
		{
			if( strncmp( buf, "processor\t", 10 ) == 0 )
			{
				ncpu++;
			}

			if( strncmp( buf, "cpu MHz\t", 8 ) == 0 &&
				ncpu < B_MAX_CPU_COUNT )
			{
				p = strchr( buf, ':' );
				if( p != NULL )
				{
					psInfo->cpu_clock_speed = atoi( p+2 );
				}
			}
		}
		fclose( fp );
	}
	psInfo->cpu_count = ncpu;

	if( (fp = fopen( "/proc/stat", "r" )) != NULL )
	{
		while( fgets( buf, sizeof(buf), fp ) != NULL )
		{
			if( ncpu == 1 && strncmp( buf, "cpu ", 4 ) == 0 )
			{
				/* there are no cpuN lines, use the overall stat */
				sscanf( buf+4, "%lu %lu %lu %lu", &n1, &n2, &n3, &nidle );
				idletime = (bigtime_t)nidle * 10000LL;
				psInfo->cpu_infos[0].active_time = systime - idletime;
				break;
			}

			if( strncmp( buf, "cpu", 3 ) == 0 )
			{
				sscanf( buf+3, "%d %lu %lu %lu %lu", &ncpu, &n1, &n2, &n3, &nidle );
				if( ncpu < psInfo->cpu_count )
				{
					idletime = (bigtime_t)nidle * 10000LL;
					psInfo->cpu_infos[ncpu].active_time = systime - idletime;
				}
			}
		}
		fclose( fp );
	}
#endif
}

/* helper for get_system_info */
static void get_mem_info( system_info* psInfo )
{
	UNIMPLEMENTED();
}


/* helper for get_system_info */
static void get_fs_info( system_info* psInfo )
{
	UNIMPLEMENTED();
}


status_t _kern_get_system_info(system_info* psInfo, size_t size )
{
	struct utsname unamebuffer;

	if (uname(&unamebuffer) == 0)
	{
		strcpy( psInfo->kernel_name, unamebuffer.sysname );
		strcpy( psInfo->kernel_build_date, unamebuffer.release );
		strcpy( psInfo->kernel_build_time, "unknown" );
	}
	else
	{
		strcpy( psInfo->kernel_name, "unknown" );
		strcpy( psInfo->kernel_build_date, "unknown" );
		strcpy( psInfo->kernel_build_time, "unknown" );
	}
	psInfo->kernel_version = 2LL;
	get_cpu_info( psInfo ); /* set boot time and cpu info */
	get_mem_info( psInfo ); /* set various mem info */
	get_fs_info( psInfo );  /* set various fs info */

	return 0;
}


status_t
_kern_get_system_info_etc(int32 id, system_info* psInfo, size_t size)
{

}


int32	_kern_is_computer_on(void)
{
	return 1L;
}


#if 0
// debugger
void
debugger(const char *message)
{
	fprintf(stderr, "debugger() called: %s\n", message);
	exit(1);
}

// _debuggerAssert
int
_debuggerAssert(const char *file, int line, const char *expression)
{
	char buffer[2048];
	snprintf(buffer, sizeof(buffer), "%s:%d: %s\n", file, line, expression);
	debugger(buffer);
	return 0;
}
#endif


// system_time
bigtime_t
system_time(void)
{
	struct timeval tm;
	gettimeofday(&tm, NULL);
	return (int64)tm.tv_sec * 1000000LL + (int64)tm.tv_usec;
}

// snooze
status_t
_kern_snooze(bigtime_t amount)
{
	if (amount <= 0)
		return B_OK;

	int64 secs = amount / 1000000LL;
	int64 usecs = amount % 1000000LL;
	if (secs > 0) {
		if (sleep((unsigned)secs) < 0)
			return errno;
	}

	if (usecs > 0) {
		if (usleep((useconds_t)usecs) < 0)
			return errno;
	}

	return B_OK;
}

// snooze_until
status_t
_kern_snooze_until(bigtime_t time, int timeBase)
{
	return snooze(time - system_time());
}


status_t
_kern_snooze_etc(bigtime_t amount, int timeBase, uint32 flags)
{
	// TODO: determine what timeBase and flags do
	return snooze(amount);
}


status_t
_kern_start_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_stop_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
