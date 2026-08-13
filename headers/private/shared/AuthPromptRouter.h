/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AUTH_PROMPT_ROUTER_H
#define _AUTH_PROMPT_ROUTER_H


#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <Autolock.h>

#include <map>
#include <vector>


namespace BPrivate {


/*!	Shared prompt-routing mechanism for daemon-invoked auth callbacks that
	must not block their own dispatch thread on a modal UI answer. BlueZ's
	Agent1 and NM's SecretAgent both need exactly this shape: hold a daemon
	method call open, hand the request to whatever UI registered itself, and
	complete the call asynchronously when (or if) an answer comes back.
	Built once so the second caller doesn't reinvent it with a different
	synchronization idiom.

	Not thread-affine: BeginRequest()/Take()/TakeAll() may be called from
	different threads (BeginRequest from the backend's GLib dispatch thread,
	Take from a UI window thread relaying the user's answer back). Callers
	own marshalling the actual daemon reply (e.g.
	g_dbus_method_invocation_return_*) onto whatever thread that requires --
	this class only owns the request-id correlation table and the single
	registered UI messenger.
*/
class AuthPromptRouter {
public:
	AuthPromptRouter()
		:
		fNextId(1)
	{
	}

	//! The UI owner (a long-lived replicant) registers itself here.
	//! Passing an invalid BMessenger clears the registration -- e.g. on
	//! DetachedFromWindow, so a later request fails fast instead of posting
	//! into the void.
	void SetUIHandler(const BMessenger& handler)
	{
		BAutolock lock(fLock);
		fUIHandler = handler;
	}

	BMessenger UIHandler()
	{
		BAutolock lock(fLock);
		return fUIHandler;
	}

	bool HasUIHandler()
	{
		BAutolock lock(fLock);
		return fUIHandler.IsValid();
	}

	//! Registers cookie under a fresh request id, adds "request_id" to
	//! requestMessage and posts it to the UI handler. Fails immediately --
	//! no id assigned, cookie not stored -- if no UI is registered or the
	//! post fails, so a daemon call waiting on the reply is never left
	//! hanging -- an unanswered agent call is indistinguishable
	//! from broken hardware).
	status_t BeginRequest(BMessage requestMessage, void* cookie,
		uint32* _requestId)
	{
		BAutolock lock(fLock);
		if (!fUIHandler.IsValid())
			return B_ERROR;

		uint32 id = fNextId++;
		requestMessage.AddUInt32("request_id", id);

		status_t status = fUIHandler.SendMessage(&requestMessage);
		if (status != B_OK)
			return status;

		fPending[id] = cookie;
		if (_requestId != NULL)
			*_requestId = id;
		return B_OK;
	}

	//! Removes and returns the cookie registered under requestId, or NULL
	//! if there is none -- already answered, already cancelled, or the
	//! request was never a blocking one. Idempotent by construction: a
	//! second call for the same id is a harmless no-op, which is exactly
	//! what a Cancel racing a just-delivered answer needs.
	void* Take(uint32 requestId)
	{
		BAutolock lock(fLock);
		std::map<uint32, void*>::iterator it = fPending.find(requestId);
		if (it == fPending.end())
			return NULL;
		void* cookie = it->second;
		fPending.erase(it);
		return cookie;
	}

	//! Empties the table, returning every still-pending cookie so the
	//! caller can fail each one (agent Cancel()/Release(), or teardown).
	void TakeAll(std::vector<void*>& cookies)
	{
		BAutolock lock(fLock);
		for (std::map<uint32, void*>::iterator it = fPending.begin();
				it != fPending.end(); ++it) {
			cookies.push_back(it->second);
		}
		fPending.clear();
	}

private:
	BLocker					fLock;
	BMessenger				fUIHandler;
	std::map<uint32, void*>	fPending;
	uint32					fNextId;
};


}	// namespace BPrivate


#endif	// _AUTH_PROMPT_ROUTER_H
