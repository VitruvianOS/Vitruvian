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


#ifndef __SPINNER_H__
#define __SPINNER_H__

#include <Control.h>

#include <string>


class BTextView;

/** 
 * \ingroup interface
 * \par Description:
 *
 * \sa
 * \author	Kurt Skauen (kurt@atheos.cx), Bill Hayden (hayden@haydentech.com)
 *****************************************************************************/

class Spinner : public BControl
{
public:
					Spinner( BRect inFrame,
							 const char* inName,
							 double vValue,
							 BMessage* pcMessage,
							 uint32 nResizeMask = B_FOLLOW_LEFT | B_FOLLOW_TOP,
							 uint32 nFlags  = B_WILL_DRAW );

	void			SetEnabled( bool bEnabled = true );
	bool			IsEnabled() const;

	void			SetFormat( const char* pzStr );
	const std::string& GetFormat() const;
	void			SetMinValue( double vValue );
	void			SetMaxValue( double vValue );
	void			SetMinMax( double vMin, double vMax ) { SetMinValue( vMin ); SetMaxValue( vMax ); }
	void			SetStep( double vStep );
	void			SetScale( double vScale );

	void			SetMinPreferredSize( int nWidthChars );
	void			SetMaxPreferredSize( int nWidthChars );

	double			GetMinValue() const;
	double			GetMaxValue() const;
	double			GetStep() const;
	double			GetScale() const;


	//virtual void	PostValueChange( const Variant& cNewValue );
	virtual void	LabelChanged( const std::string& cNewLabel );
	virtual void	EnableStatusChanged( bool bIsEnabled );

	virtual void	MouseMoved( BPoint cNewPos, uint32 nCode, const BMessage* pcData );
	virtual void	MouseDown( BPoint cPosition );
	virtual void	MouseUp( BPoint cPosition );
	virtual void	AllAttached();
	virtual void	MessageReceived( BMessage* pcMessage );

	virtual void	Draw( BRect cUpdateRect );
	virtual void	FrameResized( float inWidth, float inHeight );
	virtual void	GetPreferredSize( float* outWidth, float* outHeight );

protected:
	virtual std::string	FormatString( double vValue );

private:
	void UpdateEditBox();

	double	    m_vMinValue;
	double	    m_vMaxValue;
	double	    m_vSpeedScale;
	double	    m_vStep;
	double 	    m_vHitValue;
	BPoint 	    m_cHitPos;
	std::string	m_cStrFormat;
	BRect	    m_cEditFrame;
	BRect	    m_cUpArrowRect;
	BRect	    m_cDownArrowRect;
	bool	    m_bUpButtonPushed;
	bool	    m_bDownButtonPushed;
	BTextView*  m_pcEditBox;
};


#endif // __SPINNER_H__
