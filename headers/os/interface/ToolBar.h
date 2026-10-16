/*
 * Copyright 2011-2015, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _TOOLBAR_H
#define _TOOLBAR_H


#include <GroupView.h>
#include <List.h>


class BBitmap;
class BButton;
class BMessage;

enum {
	B_TOOLBAR_LABEL_NONE		= 0,
		// icon only
	B_TOOLBAR_LABEL_UNDER		= 1,
		// label under the icon
};


class BToolBar : public BGroupView {
public:
								BToolBar(BRect frame,
											orientation direction
											= B_HORIZONTAL);
								BToolBar(orientation direction
											= B_HORIZONTAL);
	virtual						~BToolBar();

	virtual	void				Hide();

			void				AddAction(uint32 command, BHandler* target,
									const BBitmap* icon,
									const char* toolTipText = NULL,
									const char* text = NULL,
									bool lockable = false);
			void				AddAction(BMessage* message, BHandler* target,
									const BBitmap* icon,
									const char* toolTipText = NULL,
									const char* text = NULL,
									bool lockable = false);
			void				AddAction(uint32 command, BHandler* target,
									const uint8* hvifIcon, size_t hvifLength,
									const char* toolTipText = NULL,
									const char* text = NULL,
									bool lockable = false);
			void				AddSeparator();
			void				AddGlue();
			void				AddView(BView* view);

			void				SetActionEnabled(uint32 command, bool enabled);
			void				SetActionPressed(uint32 command, bool pressed);
			void				SetActionVisible(uint32 command, bool visible);
			void				SetActionGroup(uint32 command, uint32 group);
				// same non-zero group: pressing one unpresses the others

			void				SetLabelMode(uint32 mode);
			uint32				LabelMode() const;

			void				SetOverflowEnabled(bool enabled);
			bool				IsOverflowEnabled() const;

			BButton*			FindButton(uint32 command) const;

private:
			struct ActionItem;

			friend class ToolBarButton;
			friend class OverflowButton;

	virtual	void				Pulse();
	virtual	void				FrameResized(float width, float height);

			void				_Init();
			void				_HideToolTips() const;
			void				_SetRadioPressed(ActionItem* item);
			void				_UpdateOverflow();
			ActionItem*			_FindItem(uint32 command) const;
			ActionItem*			_ItemForButton(BButton* button) const;

			orientation			fOrientation;
			uint32				fLabelMode;
			bool				fOverflowEnabled;
			bool				fInOverflowUpdate;
			BList				fItems;
			BList				fOwnedIcons;
			BButton*			fOverflowButton;

private:
			typedef BGroupView	_inherited;
};

#endif // _TOOLBAR_H
