/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PROXY_VIEW_H
#define PROXY_VIEW_H


#include "ProxySettings.h"

#include <String.h>
#include <View.h>


class BButton;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextControl;


// Proxy pane (None, Manual, Automatic with a PAC URL) for the whole
// session; dirty tracking mirrors StaticIPView.
class ProxyView : public BView {
public:
			// Posted to Window() whenever IsDirty() may have changed.
			static const uint32 kMsgDirtyChanged = 'pDty';

							ProxyView();
	virtual					~ProxyView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			// Reloads from disk into the fields and re-snapshots.
			void			Reload();

			bool			IsDirty() const;
			void			Revert();

			// NULL when Apply would be enabled; otherwise the
			// human-readable reason it's disabled.
			const char*		ReasonApplyDisabled() const;

private:
			void			_CaptureSnapshot();
			void			_UpdateApplyState();
			void			_SetMode(uint32 mode);
			void			_EnableFields(bool enable);
			uint32			_Mode() const;
			void			_DoApply();
			void			_ApplyResult(bool ok, const BString& reason);
			void			_FillFieldsFromSettings();
			void			_ReadFieldsIntoSettings();
			void			_SetField(BTextControl* control, const char* text);
			void			_SetPort(BTextControl* control, uint16 port);
			void			_FormatPair(BTextControl* hostField,
								BTextControl* portField, BString& out) const;

			// Runs Apply off the window thread; pkexec may prompt.
			static status_t	_ApplyThread(void* data);

			ProxySettings	fSettings;

			BPopUpMenu*		fModePopUpMenu;
			BMenuField*		fModeField;

			BTextControl*	fHTTPHostField;
			BTextControl*	fHTTPPortField;
			BTextControl*	fHTTPSHostField;
			BTextControl*	fHTTPSPortField;
			BTextControl*	fFTPHostField;
			BTextControl*	fFTPPortField;
			BTextControl*	fSOCKSHostField;
			BTextControl*	fSOCKSPortField;
			BTextControl*	fIgnoreHostsField;
			BTextControl*	fPACURLField;

			BButton*		fApplyButton;
			BButton*		fRevertButton;
			BStringView*	fReasonView;

			uint32			fSnapshotMode;
			BString			fSnapshotHTTPHost;
			uint16			fSnapshotHTTPPort;
			BString			fSnapshotHTTPSHost;
			uint16			fSnapshotHTTPSPort;
			BString			fSnapshotFTPHost;
			uint16			fSnapshotFTPPort;
			BString			fSnapshotSOCKSHost;
			uint16			fSnapshotSOCKSPort;
			BString			fSnapshotIgnoreHosts;
			BString			fSnapshotPACURL;

			bool			fApplyPending;
};


#endif // PROXY_VIEW_H
