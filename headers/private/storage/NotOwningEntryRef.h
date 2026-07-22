/*
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */
#ifndef _NOT_OWNING_ENTRY_REF_H
#define _NOT_OWNING_ENTRY_REF_H


#include <Entry.h>
#include <Node.h>
#include <VRefCache.h>


namespace BPrivate {


/*!	entry_ref subclass that avoids cloning the entry name.

	Therefore initialization is cheaper and cannot fail. It derives from
	entry_ref for convenience. However, care must be taken that:
	 - the name remains valid while the object is in use,
	 - the object isn't passed as an entry_ref to a function that modifies it.
 */
class NotOwningEntryRef : public entry_ref {
public:
	NotOwningEntryRef()
	{
	}

	NotOwningEntryRef(dev_t device, ino_t directory, const char* name)
	{
		SetTo(device, directory, name);
	}

	NotOwningEntryRef(const node_ref& directoryRef, const char* name)
	{
		SetTo(directoryRef, name);
	}

	NotOwningEntryRef(const entry_ref& other)
	{
		*this = other;
	}

	~NotOwningEntryRef()
	{
		// We don't own the name pointer (set via SetTo without strdup),
		// so null it out before unset() calls free() via set_name().
		name = NULL;
		unset();
	}

	NotOwningEntryRef& SetTo(dev_t device, ino_t directory, const char* name)
	{
		// Drop any previously held slot before re-targeting.
		if (is_virtual() && this->virtual_directory != B_INVALID_INO
			&& cache_ticket != BPrivate::B_INVALID_VREF_TICKET) {
			BPrivate::VRefCache::Release((vref_id) this->virtual_directory,
				cache_ticket);
		}
		cache_ticket = BPrivate::B_INVALID_VREF_TICKET;
		this->virtual_device = device;
		this->virtual_directory = directory;
		this->real_device = device;
		this->real_directory = directory;
		this->name = const_cast<char*>(name);
		if (is_virtual() && directory != B_INVALID_INO) {
			cache_ticket = BPrivate::VRefCache::Acquire((vref_id) directory);
			if (cache_ticket == BPrivate::B_INVALID_VREF_TICKET) {
				this->virtual_directory = B_INVALID_INO;
				this->virtual_device = B_INVALID_DEV;
			}
		}

		return *this;
	}

	NotOwningEntryRef& SetTo(const node_ref& directoryRef, const char* name)
	{
		return SetTo(directoryRef.vdevice(), directoryRef.vnode(), name);
	}

	node_ref DirectoryNodeRef() const
	{
		return node_ref(virtual_device, virtual_directory);
	}

	NotOwningEntryRef& SetDirectoryNodeRef(const node_ref& directoryRef)
	{
		const char* savedName = this->name;
		return SetTo(directoryRef.vdevice(), directoryRef.vnode(), savedName);
	}

	NotOwningEntryRef& operator=(const entry_ref& other)
	{
		return SetTo(other.vdevice(), other.vdirectory(), other.name);
	}
};


} // namespace BPrivate


using ::BPrivate::NotOwningEntryRef;


#endif	// _NOT_OWNING_ENTRY_REF_H
