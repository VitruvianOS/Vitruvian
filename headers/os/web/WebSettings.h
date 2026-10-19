/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_SETTINGS_H
#define WEB_SETTINGS_H


#include <SupportDefs.h>

#include <map>
#include <string>


// NetPlus profile preferences: a key=value file loaded at start and
// written on change. Keys the build does not know are preserved on
// save. Backed by the SettingsParser in the kit's private sources.
class BWebSettings {
public:
								BWebSettings();
								~BWebSettings();

			status_t			Load(const char* path);
			status_t			Save() const;

			// Points the settings at a file without reading it;
			// Save then creates it with the current values.
			void				SetPath(const char* path);
			const std::string&	Path() const { return fPath; }

			// Missing keys return the default; Set stores the value
			// and marks the settings dirty.
			const char*			Homepage() const;
			void				SetHomepage(const char* value);

			const char*			SearchEngine() const;
			void				SetSearchEngine(const char* value);

			const char*			SearchURLTemplate() const;
			void				SetSearchURLTemplate(const char* value);

			const char*			DownloadDir() const;
			void				SetDownloadDir(const char* value);

			bool				AskDownloadLocation() const;
			void				SetAskDownloadLocation(bool ask);

			bool				JavaScriptEnabled() const;
			void				SetJavaScriptEnabled(bool enabled);

			bool				ImagesEnabled() const;
			void				SetImagesEnabled(bool enabled);

			const char*			UserAgentSuffix() const;
			void				SetUserAgentSuffix(const char* value);

			const char*			BlockedDomains() const;
			void				SetBlockedDomains(const char* value);

			bool				PrivateBrowsing() const;
			void				SetPrivateBrowsing(bool priv);

			double				ZoomDefault() const;
			void				SetZoomDefault(double zoom);

			// web_autoplay_policy value; unknown values read as allow.
			int32				AutoplayPolicy() const;
			void				SetAutoplayPolicy(int32 policy);

			bool				RestoreSession() const;
			void				SetRestoreSession(bool restore);

			// Raw access so the settings window can bind new keys
			// without growing this class forever.
	const std::map<std::string, std::string>&
							Values() const { return fValues; }
			void				SetValue(const char* key,
									const char* value);

private:
			std::string			_Get(const char* key) const;
			const char*			_GetString(const char* key,
									const char* fallback) const;
			void				_Set(const char* key,
									const std::string& value);
			static bool			_ToBool(const std::string& value,
									bool fallback);

	std::map<std::string, std::string>	fValues;
			std::string			fPath;
};


#endif // WEB_SETTINGS_H
