RunnableAddOn(OverlayImage
	SOURCES
	OverlayApp.cpp
	OverlayView.cpp
	OverlayWindow.cpp
	LIBS translation localestub
	RDEF OverlayImage.rdef
)
UsePrivateHeaders(OverlayImage interface shared)
DoCatalogs("x-vnd.vos-OverlayImage" apps/overlayimage)
