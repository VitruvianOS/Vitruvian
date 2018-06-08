/*  libcosmoe.so - the interface to the Cosmoe UI
 *  Portions Copyright (C) 2001-2002 Bill Hayden
 *  Portions Copyright (C) 1999-2001 Kurt Skauen
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License version 2 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 *  MA 02111-1307, USA
 */

#ifndef	_FILE_PANEL_H
#define _FILE_PANEL_H
 
#ifndef _BE_BUILD_H
#include <BeBuild.h>
#endif

#include <sys/stat.h>
#include <Entry.h>
#include <Directory.h>
#include <Node.h>

#include <Window.h>
#include <string>



class DirectoryView;
class BTextView;
class BButton;

/*---BRefFilter--------------------------------------------------*/

/* BRefFilter is used by BFilePanel.  Every time the user selects
 * an item in a file panel, the Filter() func is sent to the
 * panel's BRefFilter object.  Filter()'s return value determines
 * whether the item is admitted into the panel's list of 
 * displayed items.  Optional.
 */
class BRefFilter {
public:
virtual	bool	Filter(const char* pzPath, const struct stat * psStat ) = 0;
};



/*---File Panel constants etc------------------------------------*/

enum file_panel_mode {
	B_OPEN_PANEL,
	B_SAVE_PANEL
};

enum file_panel_button {
	B_CANCEL_BUTTON,
	B_DEFAULT_BUTTON
};

class BWindow;
class BMessenger;
class BMessage;

/*---------------------------------------------------------------*/
/*----BFilePanel-------------------------------------------------*/

class BFilePanel : public BWindow
{
public:
    enum { NODE_FILE = 0x01, NODE_DIR = 0x02 };
  
					BFilePanel( file_panel_mode mode = B_OPEN_PANEL,
							BMessenger *target = NULL,
							const char* pzPath = NULL,
							uint32 node_flavors = NODE_FILE,
							bool allow_multiple_selection = true,
							BMessage *message = NULL,
							BRefFilter* pcFilter = NULL,
							bool  modal = false,
							bool hide_when_done = true,
							const char* pzOkLabel = NULL,
							const char* pzCancelLabel = NULL );
    virtual void	MessageReceived( BMessage* pcMessage );
    virtual void	FrameResized( float inWidth, float inHeight );

    void			SetPath( const std::string& cPath );
    std::string		GetPath() const;
	
private:
	void Layout();
	
	enum { ID_PATH_CHANGED = 1,
		   ID_SEL_CHANGED,
		   ID_INVOKED,
		   ID_CANCEL,
		   ID_OK,
		   ID_ALERT };

	BMessage*	    m_pcMessage;
	BMessenger*	    m_pcTarget;

	file_panel_mode m_nMode;
	uint32	        m_nNodeType;
	bool	        m_bHideWhenDone;
	DirectoryView*  m_pcDirView;
	BTextView*	    m_pcPathView;
	BButton*	    m_pcOkButton;
	BButton*	    m_pcCancelButton;
};


#endif
