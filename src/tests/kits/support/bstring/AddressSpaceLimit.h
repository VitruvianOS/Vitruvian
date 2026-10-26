#ifndef ADDRESS_SPACE_LIMIT_H
#define ADDRESS_SPACE_LIMIT_H

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>


class AddressSpaceLimit {
public:
	AddressSpaceLimit(rlim_t headroom = 256UL * 1024 * 1024)
	{
		fValid = getrlimit(RLIMIT_AS, &fSaved) == 0;
		if (!fValid)
			return;

		unsigned long pages = 0;
		FILE* file = fopen("/proc/self/statm", "r");
		if (file != NULL) {
			if (fscanf(file, "%lu", &pages) != 1)
				pages = 0;
			fclose(file);
		}

		struct rlimit limit = fSaved;
		limit.rlim_cur = (rlim_t)pages * sysconf(_SC_PAGESIZE) + headroom;
		if (pages == 0 || limit.rlim_cur > fSaved.rlim_max)
			fValid = false;
		else
			setrlimit(RLIMIT_AS, &limit);
	}

	~AddressSpaceLimit()
	{
		if (fValid)
			setrlimit(RLIMIT_AS, &fSaved);
	}

private:
	struct rlimit	fSaved;
	bool			fValid;
};


#endif	// ADDRESS_SPACE_LIMIT_H
