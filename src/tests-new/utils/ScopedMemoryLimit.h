// Temporarily limits how much more address space the test process may use.
//
// Tests that check how an API handles a failed allocation cannot rely on a
// huge request failing: Linux overcommits memory, so the allocation may
// succeed and exhaust the machine instead. 
#ifndef _SCOPED_MEMORY_LIMIT_H
#define _SCOPED_MEMORY_LIMIT_H

#include <sys/resource.h>
#include <unistd.h>

#include <fstream>


class ScopedMemoryLimit {
public:
	explicit ScopedMemoryLimit(rlim_t headroom = 256 * 1024 * 1024)
	{
		fValid = getrlimit(RLIMIT_AS, &fPrevious) == 0;
		if (!fValid)
			return;

		struct rlimit limit = fPrevious;
		limit.rlim_cur = _CurrentAddressSpace() + headroom;
		if (fPrevious.rlim_max != RLIM_INFINITY
			&& limit.rlim_cur > fPrevious.rlim_max) {
			limit.rlim_cur = fPrevious.rlim_max;
		}
		setrlimit(RLIMIT_AS, &limit);
	}

	~ScopedMemoryLimit()
	{
		if (fValid)
			setrlimit(RLIMIT_AS, &fPrevious);
	}

	ScopedMemoryLimit(const ScopedMemoryLimit&) = delete;
	ScopedMemoryLimit& operator=(const ScopedMemoryLimit&) = delete;

private:
	static rlim_t _CurrentAddressSpace()
	{
		// The first field of /proc/self/statm is the total program size in
		// pages.
		std::ifstream statm("/proc/self/statm");
		rlim_t pages = 0;
		statm >> pages;
		return pages * sysconf(_SC_PAGESIZE);
	}

	struct rlimit	fPrevious;
	bool			fValid;
};


#endif	// _SCOPED_MEMORY_LIMIT_H
