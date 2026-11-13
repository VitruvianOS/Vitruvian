/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_EXTENSIONS_H
#define WEB_EXTENSIONS_H


#include <Messenger.h>
#include <SupportDefs.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>


// One installed extension as the UI lists it.
struct WebExtensionInfo {
	std::string	id;
	std::string	name;
	std::string	version;
	// "content-blocker", "userscript" or "user-style".
	std::string	kind;
	std::string	licence;
	std::string	description;
	std::string	path;
	bool		enabled;
	bool		userOwned;
	// False when a content blocker ships without any of its compiled
	// rule files: the UI lists it as unavailable and no payload uses it.
	bool		available;
};


// Payloads handed to a view for one page load.
struct WebExtensionScript {
	std::string	id;
	std::string	source;
	bool		atDocumentStart = false;
	bool		isolated = false;
		// runs in the userscript world, not the page's: the GM.* bridge
		// is only reachable from there
};


struct WebExtensionStyle {
	std::string	id;
	std::string	css;
};


struct WebExtensionFilter {
	std::string	id;
	// Safari content-blocker JSON on disk; the view compiles it
	// once into the profile cache and only loads it afterwards.
	std::string	rulesPath;
};


// Full per-extension record; defined in the kit's implementation.
struct WebExtensionRecord;


/*!	NetPlus extension registry: scans the packaged and user
	extension roots, keeps the enable and per-site state, and hands
	out the payloads a view applies for a given URL. Pure file and
	state logic; the WebKit side lives in the view.
*/
class BWebExtensions {
public:
								BWebExtensions();
								~BWebExtensions();

			// Scans packagedRoot then userRoot (each holding one
			// directory per extension with a manifest.json) and
			// reads the persisted enable/per-site state.
			status_t			Load(const char* packagedRoot,
									const char* userRoot,
									const char* statePath);
			void				Reload();

			std::vector<WebExtensionInfo>
								List() const;
			bool				Get(const char* id,
									WebExtensionInfo* into) const;

			status_t			SetEnabled(const char* id, bool enabled);
			bool				IsEnabled(const char* id) const;

			// Per-site exception: the extension stays installed and
			// enabled but does not apply on the given host.
			status_t			SetDisabledOnSite(const char* id,
									const char* host, bool disabled);
			bool				IsDisabledOnSite(const char* id,
									const char* host) const;
			std::vector<std::string>
								SitesDisabledOn(const char* id) const;

			// Install copies sourcePath (a folder holding a
			// manifest.json) into the user root; remove deletes a
			// user-installed extension.
			status_t			InstallFromFolder(const char* sourcePath,
									std::string* errorInto = NULL);
			status_t			RemoveUserExtension(const char* id,
									std::string* errorInto = NULL);

			// GM.* storage for userscripts, persisted per extension
			// under the user root.
			bool				GetValue(const char* id, const char* key,
									std::string* into) const;
			status_t			SetValue(const char* id, const char* key,
									const char* value);
			// Whether id names an installed, enabled userscript that
			// uses GM.*: the GM bridge serves no one else.
			bool				AllowsGM(const char* id) const;

			// Payloads for the given page URL: enabled extensions
			// whose match patterns cover the URL and that are not
			// disabled on its host.
			std::vector<WebExtensionScript>
								ScriptsForURL(const char* url) const;
			std::vector<WebExtensionStyle>
								StylesForURL(const char* url) const;
			std::vector<WebExtensionFilter>
								FiltersForURL(const char* url) const;
			// Every enabled filter, for the one-time compile pass.
			std::vector<WebExtensionFilter>
								AllFilters() const;
			// True when a matching userscript may call GM.*.
			bool				UsesGM(const char* url) const;

			// UI notifications: B_WEB_EXTENSIONS_CHANGED to the
			// target on every persisted change.
			void				SetTarget(const BMessenger& target);

private:
			void				_ScanRoot(const std::string& root,
									bool userOwned);
			void				_LoadState();
			void				_SaveState() const;
			void				_Notify();
			bool				_AppliesTo(const WebExtensionRecord& extension,
									const std::string& url) const;

	mutable	std::mutex			fMutex;
			std::vector<WebExtensionRecord>	fExtensions;
			std::map<std::string, bool>	fEnabled;
			// extension id -> hosts the extension is off on.
			std::map<std::string, std::vector<std::string> >
								fDisabledSites;
			std::string			fPackagedRoot;
			std::string			fUserRoot;
			std::string			fStatePath;
			BMessenger			fTarget;
};


// True when `pattern` (a WebKit match pattern or <all_urls>) covers
// `url`. Exposed for tests.
bool					WebExtensionMatchPattern(const char* pattern,
							const char* url);


#endif // WEB_EXTENSIONS_H
