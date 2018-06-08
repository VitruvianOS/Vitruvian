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


#ifndef __DIRECTORYVIEW_H__
#define __DIRECTORYVIEW_H__

#include <sys/stat.h>
#include <ListView.h>
#include <Path.h>

#include <vector>
#include <string>
#include <stack>

namespace cosmoe_private
{
    class DirKeeper;
}



class BMessage;
class BBitmap;


/** Directory browser control.
 * \ingroup interface
 * \par Description:
 *
 * \sa
 * \author	Kurt Skauen (kurt@atheos.cx)
 *****************************************************************************/

class FileRow : public BListItem
{
public:
	FileRow( BBitmap* pcBitmap, const std::string& cName, const struct stat& sStat ) : m_cName(cName) {
	m_sStat = sStat; m_pcIconBitmap = pcBitmap;
	}
	virtual void	AttachToView( BView* pcView, int nColumn );
	virtual void	SetRect( const BRect& cRect, int nColumn );

	virtual float	Width( BView* pcView, int nIndex );
	virtual float	Height( BView* pcView );
	virtual void	Draw( const BRect& cFrame,
						  BView* pcView,
						  uint nColumn,
						  bool bSelected,
						  bool bHighlighted,
						  bool bHasFocus );
	virtual bool	HitTest( BView* pcView, const BRect& cFrame, int nColumn, BPoint cPos );
	virtual bool	IsLessThan( const BListItem* pcOther, uint nColumn ) const;
	const std::string& GetName() const { return( m_cName ); }
	struct stat		GetFileStat() const { return( m_sStat ); }
private:
	friend class	DirectoryView;
	std::string		m_cName;
	struct stat		m_sStat;
	uint8       	m_anIcon[16*16*4];
	BBitmap*		m_pcIconBitmap;
	float			m_avWidths[7];
};

/** Directory view suitable for file-requesters and other file browsers.
 * \ingroup interface
 * \par Description:
 *
 * \sa FileRequester
 * \author	Kurt Skauen (kurt@atheos.cx)
 *****************************************************************************/

class DirectoryView : public BListView
{
#if 0
public:
					DirectoryView( const BRect& cFrame,
								   const std::string& cPath,
								   uint32 nModeFlags = F_MULTI_SELECT |
													   F_RENDER_BORDER,
								   uint32 nResizeMask = B_FOLLOW_LEFT |
														B_FOLLOW_TOP,
								   uint32 nViewFlags = B_WILL_DRAW |
													   B_FULL_UPDATE_ON_RESIZE );
					~DirectoryView();

	void			ReRead();
	void			SetPath( const std::string& cPath );
	std::string		GetPath() const;

	FileRow*		GetFile( int nRow ) const { return( static_cast<FileRow*>(GetRow(nRow)) ); }
	void			SetDirChangeMsg( BMessage* pcMsg );
//  void	SetDirChangeTarget( const BHandler* pcHandler, const BLooper* pcLooper = NULL );
//  void	SetDirChangeTarget( const Messenger& cTarget );

	virtual void	DirChanged( const std::string& cNewPath );
	virtual void	Invoked( int nFirstRow, int nLastRow );
	virtual bool	DragSelection( const BPoint& cPos );
	virtual void	MessageReceived( BMessage* pcMessage );
	virtual void	AttachedToWindow();
	virtual void	DetachedFromWindow();
	virtual void	MouseUp( BPoint cPosition );
	virtual void	MouseMoved( BPoint cNewPos, uint32 nCode, const BMessage* pcData );
	virtual void	KeyDown(const char* bytes, int32 numBytes);
private:
	enum { M_CLEAR, M_ADD_ENTRY, M_UPDATE_ENTRY, M_REMOVE_ENTRY };
	friend class cosmoe_private::DirKeeper;

	static int32	ReadDirectory( void* pData );
	void			PopState();

	struct State
	{
		State( BListView* pcView, const char* pzPath );
		std::string			 		m_cPath;
		uint						m_nScrollOffset;
		std::vector<std::string>	m_cSelection;
	};
	
	struct ReadDirParam
	{
		ReadDirParam( DirectoryView* pcView ) { m_pcView = pcView; m_bCancel = false; }
		DirectoryView*	m_pcView;
		bool			m_bCancel;
	};
	
	ReadDirParam*		m_pcCurReadDirSession;
	BMessage*			m_pcDirChangeMsg;
	BPath				m_cPath;
//    Directory	      m_cDirectory;
	cosmoe_private::DirKeeper*	m_pcDirKeeper;
	std::string			m_cSearchString;
	bigtime_t			m_nLastKeyDownTime;
	std::stack<State>	m_cStack;
	BBitmap*			m_pcIconBitmap;
#endif
};


#endif // __DIRECTORYVIEW_H__
