/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PROXY_SETTINGS_H
#define PROXY_SETTINGS_H


#include <String.h>
#include <SettingsMessage.h>


// Session-wide proxy, stored in config/settings/network_proxy; janus_session
// exports it at login and Apply refreshes apt's root drop-in.
class ProxySettings {
public:
	enum Mode {
		MODE_NONE = 0,
		MODE_MANUAL,
		MODE_AUTOMATIC
	};

							ProxySettings();

			uint32			Mode() const { return fMode; }
			const BString&	HTTPHost() const { return fHTTPHost; }
			uint16			HTTPPort() const { return fHTTPPort; }
			const BString&	HTTPSHost() const { return fHTTPSHost; }
			uint16			HTTPSPort() const { return fHTTPSPort; }
			const BString&	FTPHost() const { return fFTPHost; }
			uint16			FTPPort() const { return fFTPPort; }
			const BString&	SOCKSHost() const { return fSOCKSHost; }
			uint16			SOCKSPort() const { return fSOCKSPort; }
			const BString&	IgnoreHosts() const { return fIgnoreHosts; }
			const BString&	PACURL() const { return fPACURL; }

			void			SetMode(uint32 mode) { fMode = mode; }
			void			SetHTTPHost(const BString& host) { fHTTPHost = host; }
			void			SetHTTPPort(uint16 port) { fHTTPPort = port; }
			void			SetHTTPSHost(const BString& host) { fHTTPSHost = host; }
			void			SetHTTPSPort(uint16 port) { fHTTPSPort = port; }
			void			SetFTPHost(const BString& host) { fFTPHost = host; }
			void			SetFTPPort(uint16 port) { fFTPPort = port; }
			void			SetSOCKSHost(const BString& host) { fSOCKSHost = host; }
			void			SetSOCKSPort(uint16 port) { fSOCKSPort = port; }
			void			SetIgnoreHosts(const BString& hosts) { fIgnoreHosts = hosts; }
			void			SetPACURL(const BString& url) { fPACURL = url; }

			void			Load();
			void			Save();

			// Writes the apt drop-in (pkexec). Session env comes from the
			// settings file on the next login; *errorMessage gets the reason.
			bool			Apply(BString* errorMessage) const;

			// Same as Apply(), from explicit values; used by the apply
			// thread so the job does not need a copyable ProxySettings.
			static bool		ApplyWith(uint32 mode, const BString& http,
								const BString& https, const BString& ftp,
								const BString& socks, const BString& ignore,
								const BString& pacURL,
								BString* errorMessage);

			// Default ignore list.
			static const char*	DefaultIgnoreHosts();

private:
			void			_FormatHostPort(const BString& host, uint16 port,
								BString& out) const;

			SettingsMessage	fSettingsMessage;

			uint32			fMode;
			BString			fHTTPHost;
			uint16			fHTTPPort;
			BString			fHTTPSHost;
			uint16			fHTTPSPort;
			BString			fFTPHost;
			uint16			fFTPPort;
			BString			fSOCKSHost;
			uint16			fSOCKSPort;
			BString			fIgnoreHosts;
			BString			fPACURL;
};


#endif // PROXY_SETTINGS_H
