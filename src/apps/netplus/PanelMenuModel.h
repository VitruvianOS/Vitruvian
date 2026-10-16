/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _PANEL_MENU_MODEL_H
#define _PANEL_MENU_MODEL_H


#include <stdint.h>
#include <string>
#include <vector>


enum PanelMenuRowType {
	kPanelMenuRowAction,
	kPanelMenuRowSubPage,
	kPanelMenuRowSeparator,
	kPanelMenuRowSectionLabel,
};


struct PanelMenuRow {
			PanelMenuRow();

	PanelMenuRowType	type;
	std::string			label;
	std::string			shortcut;
	std::string			payload;
	uint32_t			messageWhat;
	int32_t				subPage;
	int32_t				icon;
		// index into the caller's icon table, -1 for none
	bool				enabled;
};


/*!	Page stack behind the browser-style panel menu (Firefox's hamburger
	panel, Chrome's three-dot menu): a stack of pages, each a list of
	rows. Drilling into a sub-page row pushes; the header back arrow
	pops. Plain C++ so a host test covers the logic without BViews.
*/
class PanelMenuModel {
public:
			PanelMenuModel();

			int32_t			AddPage(const char* title);

			void			AddActionRow(int32_t page, const char* label,
									uint32_t messageWhat,
									const char* shortcut = NULL,
									const char* payload = NULL,
									int32_t icon = -1);
			void			AddSubPageRow(int32_t page, const char* label,
									int32_t subPage, int32_t icon = -1);
			void			AddSeparatorRow(int32_t page);
			void			AddSectionLabelRow(int32_t page,
									const char* label);

			int32_t			CurrentPage() const;
			const char*		CurrentTitle() const;

			// True while a sub-page is on the stack: the header shows
			// the back arrow then.
			bool			CanGoBack() const;

			void			PushPage(int32_t page);
			void			PopPage();

			int32_t			RowCount(int32_t page) const;
			const PanelMenuRow&	RowAt(int32_t page, int32_t index) const;

			enum SelectResult {
				kSelectNone,
					// separator or section label row
				kSelectInvoke,
					// leaf action: invoke messageWhat with payload
				kSelectPush
					// sub-page row: push subPage
			};

			SelectResult	SelectRow(int32_t index, uint32_t* what,
									std::string* payload,
									int32_t* page) const;

private:
			struct Page {
				std::string				title;
				std::vector<PanelMenuRow>	rows;
			};

			const PanelMenuRow&	_RowAt(int32_t page, int32_t index) const;

			std::vector<Page>	fPages;
			std::vector<int32_t>	fStack;
};


#endif // _PANEL_MENU_MODEL_H
