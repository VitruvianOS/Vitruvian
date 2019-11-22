#ifndef MULTI_LOCKER_H
#define MULTI_LOCKER_H

#include <RWLocker.h>

#define MultiLocker RWLocker

#define MULTI_LOCKER_DEBUG 0
#define ASSERT_MULTI_LOCKED(x) ;
#define ASSERT_MULTI_READ_LOCKED(x) ;
#define ASSERT_MULTI_WRITE_LOCKED(x) ;

#endif	// MULTI_LOCKER_H
