/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_CONTROLLABLE_PRIVATE_H
#define _MEDIA2_CONTROLLABLE_PRIVATE_H


#include <media2/Controllable.h>


namespace BPrivate {
namespace media {


// Backend access to BControllable internals.
class ControllablePrivate {
public:
	static	void	ApplyProps(BControllable* controllable, const void* pod)
						{ controllable->_ApplyProps(pod); }
};


}	// namespace media
}	// namespace BPrivate


#endif	// _MEDIA2_CONTROLLABLE_PRIVATE_H
