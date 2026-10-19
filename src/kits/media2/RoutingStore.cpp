/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/RoutingStore.h>

#include <fcntl.h>
#include <mutex>
#include <new>
#include <vector>

#include <Autolock.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Message.h>


static const uint32 kRoutingMagic = 'MdlR';


static bool
_SameLink(const BMediaRoute& a, const BMediaRoute& b)
{
	return a.fromNode == b.fromNode && a.fromPort == b.fromPort
		&& a.toNode == b.toNode && a.toPort == b.toPort;
}


struct BRoutingStore::Impl {
	Impl()
		:
		lock("routing store")
	{
	}

	BLocker						lock;
	BPath						path;
	std::vector<BMediaRoute>	routes;
};


BRoutingStore*
BRoutingStore::Instance()
{
	static std::once_flag once;
	static BRoutingStore* instance = NULL;
	std::call_once(once, [] {
		instance = new(std::nothrow) BRoutingStore();
		if (instance != NULL)
			instance->Load();
	});
	return instance;
}


BRoutingStore::BRoutingStore()
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;

	BPath directory;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &directory, true) != B_OK
		|| directory.Append("Media") != B_OK
		|| create_directory(directory.Path(), 0755) != B_OK
		|| directory.Append("routing") != B_OK) {
		return;
	}
	fImpl->path = directory;
}


BRoutingStore::~BRoutingStore()
{
	delete fImpl;
}


status_t
BRoutingStore::Load()
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	BAutolock _(fImpl->lock);
	fImpl->routes.clear();
	if (fImpl->path.InitCheck() != B_OK)
		return B_ERROR;

	BFile file(fImpl->path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_OK;

	BMessage message;
	status_t status = message.Unflatten(&file);
	if (status != B_OK)
		return status;
	if (message.what != kRoutingMagic)
		return B_BAD_VALUE;

	BMediaRoute route;
	for (int32 i = 0; message.FindString("from node", i, &route.fromNode)
			== B_OK; i++) {
		message.FindString("from port", i, &route.fromPort);
		message.FindString("to node", i, &route.toNode);
		message.FindString("to port", i, &route.toPort);
		fImpl->routes.push_back(route);
	}
	return B_OK;
}


status_t
BRoutingStore::Save() const
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	BAutolock _(fImpl->lock);
	if (fImpl->path.InitCheck() != B_OK)
		return B_ERROR;

	BMessage message(kRoutingMagic);
	for (size_t i = 0; i < fImpl->routes.size(); i++) {
		const BMediaRoute& route = fImpl->routes[i];
		message.AddString("from node", route.fromNode);
		message.AddString("from port", route.fromPort);
		message.AddString("to node", route.toNode);
		message.AddString("to port", route.toPort);
	}

	BFile file(fImpl->path.Path(), B_CREATE_FILE | B_WRITE_ONLY | O_TRUNC);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	return message.Flatten(&file);
}


status_t
BRoutingStore::AddRoute(const BMediaRoute& route)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	{
		BAutolock _(fImpl->lock);
		bool found = false;
		for (size_t i = 0; i < fImpl->routes.size(); i++) {
			if (_SameLink(fImpl->routes[i], route)) {
				found = true;
				break;
			}
		}
		if (!found)
			fImpl->routes.push_back(route);
	}
	return Save();
}


status_t
BRoutingStore::RemoveRoute(const BMediaRoute& route)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	{
		BAutolock _(fImpl->lock);
		size_t i = 0;
		while (i < fImpl->routes.size()
			&& !_SameLink(fImpl->routes[i], route)) {
			i++;
		}
		if (i == fImpl->routes.size())
			return B_ENTRY_NOT_FOUND;
		fImpl->routes.erase(fImpl->routes.begin() + i);
	}
	return Save();
}


int32
BRoutingStore::CountRoutes() const
{
	if (fImpl == NULL)
		return 0;
	BAutolock _(fImpl->lock);
	return (int32)fImpl->routes.size();
}


status_t
BRoutingStore::GetRoute(int32 index, BMediaRoute* outRoute) const
{
	if (fImpl == NULL || outRoute == NULL)
		return B_BAD_VALUE;
	BAutolock _(fImpl->lock);
	if (index < 0 || index >= (int32)fImpl->routes.size())
		return B_BAD_INDEX;
	*outRoute = fImpl->routes[index];
	return B_OK;
}
