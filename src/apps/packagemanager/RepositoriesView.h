/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef REPOSITORIES_VIEW_H
#define REPOSITORIES_VIEW_H

#include <GroupView.h>
#include <ObjectList.h>
#include <String.h>


class BButton;
class BColumnListView;
class BMessage;
class TruncatingStringView;


// One list row: a deb822 stanza, or a physical line of a one-line
// sources file. index is the stanza number for .sources files and the
// physical line number for one-line files, matching what
// vos-apt-helper source-set-enabled expects.
struct source_entry {
			BString			path;
			BString			name;
			BString			types;
			BString			uris;
			BString			suites;
			BString			components;
			BString			signedBy;
			int32			index;
			bool			enabled;
			bool			oneLine;
			bool			owned;	// vos-user-* file, editable/removable
};


class RepositoriesView : public BGroupView {
	typedef BGroupView Inherited;
public:
								RepositoriesView();
	virtual						~RepositoriesView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Reload();
			void				SetTransactionActive(bool active);
			void				UpdateButtons();
			source_entry*		SelectedEntry() const;

private:
			void				_AddColumns();
			void				_AddEntry(source_entry* entry);
			void				_ParseOneLineFile(const BString& path,
									const BString& name,
									const BString& text);
			void				_ParseSourcesFile(const BString& path,
									const BString& name,
									const BString& text);

			BColumnListView*	fListView;
			BButton*			fAddButton;
			BButton*			fEditButton;
			BButton*			fRemoveButton;
			BButton*			fToggleButton;
			BButton*			fReloadButton;
			TruncatingStringView* fHintView;
			BObjectList<source_entry, true>	fEntries;
			bool				fTransactionActive;
};


#endif // REPOSITORIES_VIEW_H
