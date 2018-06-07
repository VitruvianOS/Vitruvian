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


#include <FilePanel.h>
#include <DirectoryView.h>
#include <Region.h>
#include <TextView.h>
#include <Button.h>
#include <Alert.h>
#include <Message.h>
#include <Application.h>
#include <Debug.h>



BFilePanel::BFilePanel( file_panel_mode   nMode,
                        BMessenger*       pcTarget,
                        const char*       pzPath,
                        uint32            nNodeType,
                        bool              bMultiSelect,
                        BMessage*         pcMessage,
                        BRefFilter*       pcFilter,
                        bool              bModal,
                        bool              bHideWhenDone,
                        const char*       pzOkLabel,
                        const char*       pzCancelLabel ) : BWindow( BRect(0,0,1,1), "", B_UNTYPED_WINDOW, 0 )

{
	Lock();

	if ( pzOkLabel == NULL )
	{
		if ( nMode == B_OPEN_PANEL )
		{
			pzOkLabel = "Load";
		}
		else
		{
			pzOkLabel = "Save";
		}
	}

	if ( pzCancelLabel == NULL )
	{
		pzCancelLabel = "Cancel";
	}


	m_nMode         = nMode;
	m_nNodeType     = nNodeType;
	m_bHideWhenDone = bHideWhenDone;
	m_pcTarget      = (pcTarget != NULL ) ? pcTarget : new BMessenger( be_app );
	m_pcMessage     = pcMessage;

	std::string cPath;
	std::string cFile;

	if ( pzPath == NULL )
	{
		const char* pzHome = getenv( "HOME" );
		if ( pzHome != NULL )
		{
			cPath = pzHome;
			if ( cPath[cPath.size()-1] != '/' )
			{
				cPath += '/';
			}
		}
		else
		{
			cPath = "/tmp/";
		}
	}
	else
	{
		cPath = pzPath;
		if ( cPath.find( '/' ) == std::string::npos )
		{
			const char* pzHome = getenv( "HOME" );
			if ( pzHome != NULL ) {
				cPath = pzHome;
				if ( cPath[cPath.size()-1] != '/' )
				{
					cPath += '/';
				}
			}
			else
			{
				cPath = "/tmp/";
			}
			cFile = pzPath;
		}
		else
		{
			cPath = pzPath;
		}
	}

	if ( cFile.empty() )
	{
		if ( nNodeType == NODE_FILE )
		{
			struct stat sStat;
			if ( cPath[cPath.size()-1] != '/' || (stat( cPath.c_str(), &sStat ) < 0 || S_ISREG( sStat.st_mode )) ) {
				uint nSlash = cPath.rfind( '/' );
				if ( nSlash != std::string::npos )
				{
					cFile = cPath.c_str() + nSlash + 1;
					cPath.resize( nSlash );
				}
				else
				{
					cFile = cPath;
					cPath = "";
				}
			}
		}
	}
	m_pcDirView      = new DirectoryView( Bounds(), cPath );
	m_pcPathView     = new BTextView( BRect(0,0,1,1), "path_edit", BRect(0,0,1,1), 0 );
	m_pcCancelButton = new BButton( BRect(0,0,1,1), "cancel", pzCancelLabel, new BMessage( ID_CANCEL ) );
	m_pcOkButton     = new BButton( BRect(0,0,1,1), "ok", pzOkLabel, new BMessage( ID_OK ) );

	AddChild( m_pcDirView );
	AddChild( m_pcPathView );
	AddChild( m_pcCancelButton );
	AddChild( m_pcOkButton );

	m_pcDirView->SetMultiSelect( bMultiSelect );
	m_pcDirView->SetSelChangeMsg( new BMessage( ID_SEL_CHANGED ) );
	m_pcDirView->SetInvokeMsg( new BMessage( ID_INVOKED ) );
	m_pcDirView->SetDirChangeMsg( new BMessage( ID_PATH_CHANGED ) );
	m_pcDirView->MakeFocus();
	SetTitle( m_pcDirView->GetPath().c_str() );

	Unlock();
}


void BFilePanel::Layout()
{
	BRect cBounds = Bounds();

	BPoint cSize1;
	BPoint cSize2;

	m_pcOkButton->GetPreferredSize( &cSize1.x, &cSize1.y );
	m_pcCancelButton->GetPreferredSize( &cSize2.x, &cSize2.y );

	if ( cSize2.x > cSize1.x )
	{
		cSize1 = cSize2;
	}

	BRect cOkRect = cBounds;

	cOkRect.bottom -= 5;
	cOkRect.top = cOkRect.bottom - cSize1.y;
	cOkRect.right -= 10;
	cOkRect.left = cOkRect.right - cSize1.x;

	BRect cCancelRect = cOkRect;
	cCancelRect.left -= cSize1.x + 15;
	cCancelRect.right -= cSize1.x + 15;

	float height;
	m_pcPathView->GetPreferredSize( NULL, &height );
	BRect cPathFrame = cBounds;

	cPathFrame.bottom = cOkRect.top - 10;
	cPathFrame.top = cPathFrame.bottom - height;
	cPathFrame.left += 10;
	cPathFrame.right -= 10;

	BRect cDirFrame = cBounds;
	cDirFrame.bottom = cPathFrame.top - 10;
	cDirFrame.top += 10;
	cDirFrame.left += 10;
	cDirFrame.right -= 10;
}


void BFilePanel::FrameResized( float inWidth, float inHeight )
{
	Layout();
}


void BFilePanel::MessageReceived( BMessage* pcMessage )
{
	switch( pcMessage->what )
	{
		case ID_PATH_CHANGED:
			SetTitle( m_pcDirView->GetPath().c_str() );
			break;

		case ID_SEL_CHANGED:
		{
			int nSel = m_pcDirView->GetFirstSelected();
			if ( nSel >= 0 )
			{
				FileRow* pcFile = m_pcDirView->GetFile( nSel );
				if ( (m_nNodeType & NODE_DIR) || S_ISDIR( pcFile->GetFileStat().st_mode ) == false ) {
					m_pcPathView->SetText( pcFile->GetName().c_str() );
				}
			}
//            else {
//                m_pcPathView->SetText( "" );
//            }
			break;
		}
		case ID_ALERT: // User has answered the "Are you sure..." requester
		{
			int32 nButton;
			if ( pcMessage->FindInt32( "which", &nButton ) != 0 )
			{
				dbprintf( "BFilePanel::MessageReceived() message from alert "
						"requester does not contain a 'which' element!\n" );
				break;
			}

			if ( nButton == 0 )
			{
				break;
			}
			/*** FALL THROUGH ***/
		}
		case ID_OK:
		case ID_INVOKED:
		{
			BMessage* pcMsg;
			if ( m_pcMessage != NULL )
			{
				pcMsg = new BMessage( *m_pcMessage );
			}
			else
			{
				pcMsg = new BMessage( (m_nMode == B_OPEN_PANEL) ? M_LOAD_REQUESTED : M_SAVE_REQUESTED );
			}
			pcMsg->AddPointer( "source", this );

			if ( m_nMode == B_OPEN_PANEL )
			{
				for ( int i = m_pcDirView->GetFirstSelected() ; i <= m_pcDirView->GetLastSelected() ; ++i ) {
					if ( m_pcDirView->IsSelected( i ) == false )
					{
						continue;
					}
					BPath cPath( m_pcDirView->GetPath().c_str() );
					cPath.Append( m_pcDirView->GetFile(i)->GetName().c_str() );
					pcMsg->AddString( "file/path", cPath.Path() );
				}
				m_pcTarget->SendMessage( pcMsg );
				Hide();
			}
			else
			{
				BPath cPath( m_pcDirView->GetPath().c_str() );
				cPath.Append( m_pcPathView->Text() );

				struct stat sStat;
				if ( pcMessage->what != ID_ALERT && stat( cPath.Path(), &sStat ) >= 0 )
				{
					std::string cMsg("The file ");
					cMsg += cPath.Path();
					cMsg += " already exists\nDo you want to overwrite it?\n";

					BAlert* pcAlert = new BAlert( "Alert:", cMsg.c_str(), "No", "Yes", NULL );
					pcAlert->Go( new BInvoker( new BMessage( ID_ALERT ), this ) );
				}
				else
				{
					pcMsg->AddString( "file/path", cPath.Path() );
					m_pcTarget->SendMessage( pcMsg );
					Hide();
				}
			}
			delete pcMsg;
			break;
		}

		case ID_CANCEL:
			Hide();
			break;

		default:
			BWindow::MessageReceived( pcMessage );
			break;
	}
}

void BFilePanel::SetPath( const std::string& a_cPath )
{
	std::string cPath = a_cPath;
	std::string cFile;

	if ( cPath.find( '/' ) == std::string::npos )
	{
		const char* pzHome = getenv( "HOME" );
		if ( pzHome != NULL )
		{
			cPath = pzHome;
			if ( cPath[cPath.size()-1] != '/' )
			{
				cPath += '/';
			}
		}
		else
		{
			cPath = "/tmp/";
		}

		cFile = a_cPath;
	}
	if ( cFile.empty() )
	{
		if ( m_nNodeType == NODE_FILE )
		{
			struct stat sStat;
			if ( cPath[cPath.size()-1] != '/' || (stat( cPath.c_str(), &sStat ) < 0 || S_ISREG( sStat.st_mode )) ) {
				uint nSlash = cPath.rfind( '/' );
				if ( nSlash != std::string::npos )
				{
					cFile = cPath.c_str() + nSlash + 1;
					cPath.resize( nSlash );
				}
				else
				{
					cFile = cPath;
					cPath = "";
				}
			}
		}
	}

	m_pcPathView->SetText( cFile.c_str() );
	m_pcDirView->SetPath( cPath );
}

std::string BFilePanel::GetPath() const
{
	return( m_pcDirView->GetPath() );
}
