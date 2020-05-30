Application(
	Terminal

	SOURCES
	ActiveProcessInfo.cpp
	AppearPrefView.cpp
	Arguments.cpp
	BasicTerminalBuffer.cpp
	Colors.cpp
	FindWindow.cpp
	Globals.cpp
	HistoryBuffer.cpp
	HyperLink.cpp
	InlineInput.cpp
	PatternEvaluator.cpp
	PrefHandler.cpp
	PrefWindow.cpp
	SetTitleDialog.cpp
	Shell.cpp
	ShellInfo.cpp
	ShellParameters.cpp
	SmartTabView.cpp
	TermApp.cpp
	TerminalBuffer.cpp
	TerminalCharClassifier.cpp
	TerminalRoster.cpp
	TermConst.cpp
	TermParse.cpp
	TermScrollView.cpp
	TermView.cpp
	TermViewHighlight.cpp
	TermViewStates.cpp
	TermWindow.cpp
	TitlePlaceholderMapper.cpp
	VTKeyTbl.c
	VTPrsTbl.c

	LIBS
	tracker
	pthread
)

UsePrivateHeaders(Terminal textencoding kernel)
