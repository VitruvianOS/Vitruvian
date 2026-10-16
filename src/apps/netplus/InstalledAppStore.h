/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _INSTALLED_APP_STORE_H
#define _INSTALLED_APP_STORE_H


#include <stdint.h>
#include <string>
#include <vector>


/*!	One installed web app record: what the Deskbar symlink and the
	standalone window need to come back later.
*/
struct InstalledApp {
	std::string		id;
	std::string		name;
	std::string		startUrl;
	std::string		display;
	std::string		manifestUrl;
	std::string		iconPath;
};


/*!	NetPlus-local store of installed PWAs: one JSON record per app
	under the root directory passed in. Plain POSIX so the host test
	covers add/list/remove without BViews or WebKit.
*/
class InstalledAppStore {
public:
			InstalledAppStore(const char* rootPath);

	const std::string&	Root() const { return fRoot; }
	std::string			IconsDirectory() const;

			bool		Add(const InstalledApp& app);
			bool		Remove(const char* id);

			int32_t		Count() const { return (int32_t)fApps.size(); }
			const InstalledApp&	At(int32_t index) const;

			int32_t		IndexOf(const char* id) const;

	// Launch lookup: the Deskbar symlink carries the app name, the
	// --pwa= switch may carry either the name or the id.
			int32_t		IndexOfEntryName(const char* name) const;

	// "Wikipedia mobile" -> "wikipedia-mobile"; empty for names with
	// nothing usable left in them.
	static	std::string	IDFromName(const char* name);

private:
			void		_Load();
			bool		_WriteRecord(const InstalledApp& app) const;
			bool		_RemoveRecord(const char* id) const;

			std::string				fRoot;
			std::vector<InstalledApp>	fApps;
};


#endif // _INSTALLED_APP_STORE_H
