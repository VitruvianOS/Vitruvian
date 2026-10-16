/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _URL_BAR_VIEW_H
#define _URL_BAR_VIEW_H


#include <View.h>

#include "SuggestModel.h"


class BBitmap;
class BMessage;
class HistoryStore;
class SuggestPopup;
class UrlTextView;


/*!	Address field with an autocomplete dropdown and the connection
	security icon at the left. A custom text view instead of
	BTextControl: the inner view reports every edit, so suggestions
	track the typing without polling.
*/
class UrlBarView : public BView {
public:
								UrlBarView(BRect frame);
	virtual						~UrlBarView();

	virtual void				DetachedFromWindow();
	virtual void				MouseDown(BPoint where);
	virtual void				MessageReceived(BMessage* message);
	virtual	void				MakeFocus(bool focused = true);

			void				SetHistorySource(HistoryStore* store);
			void				SetSearchEngine(int32_t engine);

			const char*			Text() const;
			void				SetText(const char* text);
			void				SelectAll();

			// 0 none/internal, 1 plain http, 2 https
			void				SetSecurityState(uint8 state);

private:
			friend class UrlTextView;

			void				_TextChanged();
			void				_MoveSelection(int32_t delta);
			void				_AcceptSuggestion(int32_t index);
			void				_Submit();
			void				_HideSuggestions();
			void				_ShowInfo();
			BRect				_IconRect() const;

			UrlTextView*		fTextView;
			SuggestPopup*		fPopup;
			SuggestModel		fModel;
			HistoryStore*		fHistory;
			BBitmap*			fSecureIcon;
			BBitmap*			fInsecureIcon;
			int32				fSelection;
			int32_t				fSearchEngine;
			uint8				fSecurity;
			bool				fBlockNotify;
};


#endif // _URL_BAR_VIEW_H
