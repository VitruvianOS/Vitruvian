/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Erik Jaesler (erik@cgsoftware.com)
 */

/**	Extra messaging utility functions */

#include <string.h>
#include <ByteOrder.h>

#include <MessageUtils.h>

namespace BPrivate {

uint32
CalculateChecksum(const uint8 *buffer, int32 size)
{
	uint32 sum = 0;
	uint32 temp = 0;

	while (size > 3) {
		sum += B_BENDIAN_TO_HOST_INT32(*(int32 *)buffer);
		buffer += 4;
		size -= 4;
	}

	while (size > 0) {
		temp = (temp << 8) + *buffer++;
		size -= 1;
	}

	return sum + temp;
}


status_t entry_ref_flatten(char *buffer, size_t *size, const entry_ref *ref)
{
	if (*size < sizeof(dev_t) + sizeof(ino_t))
		return B_BUFFER_OVERFLOW;

	dev_t dev = ref->dev();
	ino_t dir = ref->dir();
	memcpy((void *)buffer, (const void *)&dev, sizeof(dev));
	buffer += sizeof(dev);
	memcpy((void *)buffer, (const void *)&dir, sizeof(dir));
	buffer += sizeof(dir);
	*size -= sizeof(dev) + sizeof(dir);

	size_t nameLength = 0;
	if (ref->name) {
		nameLength = strlen(ref->name) + 1;
		if (*size < nameLength)
			return B_BUFFER_OVERFLOW;
		memcpy((void *)buffer, (const void *)ref->name, nameLength);
	}
	*size = sizeof(dev) + sizeof(dir) + nameLength;
	return B_OK;
}


status_t entry_ref_unflatten(entry_ref *ref, const char *buffer, size_t size)
{
	if (size < sizeof(dev_t) + sizeof(ino_t)) {
		*ref = entry_ref();
		return B_BAD_VALUE;
	}

	dev_t dev;
	ino_t dir;
	memcpy(&dev, buffer, sizeof(dev));
	buffer += sizeof(dev);
	memcpy(&dir, buffer, sizeof(dir));
	buffer += sizeof(dir);

	if (dev != B_INVALID_DEV && size > sizeof(dev) + sizeof(dir))
		*ref = entry_ref(dev, dir, buffer);
	else
		*ref = entry_ref(dev, dir, NULL);
	
	if (ref->name == NULL && size > sizeof(dev) + sizeof(dir)) {
		*ref = entry_ref();
		return B_NO_MEMORY;
	}
	
	return B_OK;
}


status_t
entry_ref_swap(char *buffer, size_t size)
{
	if (size < sizeof(dev_t) + sizeof(ino_t))
		return B_BAD_VALUE;

	dev_t *dev = (dev_t *)buffer;
	*dev = B_SWAP_INT32(*dev);
	buffer += sizeof(dev_t);

	ino_t *ino = (ino_t *)buffer;
	*ino = B_SWAP_INT64(*ino);

	return B_OK;
}

} // namespace BPrivate
