Application(FontDemo
	SOURCES
	ControlView.cpp
	FontDemo.cpp
	FontDemoView.cpp
	LIBS localestub
	RDEF FontDemo.rdef
)
UsePrivateHeaders(FontDemo shared interface app)
