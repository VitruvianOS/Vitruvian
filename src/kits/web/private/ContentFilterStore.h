/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#pragma once

#include <glib.h>
#include <wpe/webkit.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>


/*!	Process-wide content-blocker compile cache: Safari JSON is
	compiled once into the profile store, later launches only load
	it. Callbacks run on the GLib thread.
*/
class ContentFilterStore {
public:
	static ContentFilterStore&	Instance();

			// Called once by the first bridge with the shared
			// profile directory; later calls are ignored.
			void				SetCacheDir(const std::string& dir);

			void				Get(const std::string& id,
									const std::string& jsonPath,
									std::function<void(
										WebKitUserContentFilter*)> cb);

private:
								ContentFilterStore();
								~ContentFilterStore();

			void				_GetOnGLibThread(const std::string& id,
									const std::string& jsonPath,
									std::function<void(
										WebKitUserContentFilter*)> cb);
			void				_Compile(const std::string& id,
									const std::string& jsonPath);
			void				_Finish(const std::string& id,
									WebKitUserContentFilter* filter,
									const char* what, GError* error);
			static void			_OnSaveDone(GObject* source,
									GAsyncResult* result, gpointer data);
			static void			_OnLoadDone(GObject* source,
									GAsyncResult* result, gpointer data);

			struct Pending {
				std::vector<std::function<void(
					WebKitUserContentFilter*)> >	callbacks;
			};

	mutable	std::mutex			fMutex;
			WebKitUserContentFilterStore*	fStore;
			std::string			fCacheDir;
			bool				fCacheDirSet;
			std::map<std::string, WebKitUserContentFilter*>
								fCompiled;
			std::map<std::string, Pending*>	fPending;
};
