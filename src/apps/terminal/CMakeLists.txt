add_executable(Terminal
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
)

target_include_directories(Terminal PRIVATE
	"../../../headers/private/textencoding/"
)

#localestub shared tracker translation textencoding
target_link_libraries(Terminal PRIVATE be root tracker)
