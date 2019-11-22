add_executable(miniterminal
	Arguments.cpp
	MiniApp.cpp
	MiniWin.cpp
	Console.cpp
	MiniView.cpp
	ViewBuffer.cpp
)

target_link_libraries(miniterminal PRIVATE be root)

target_include_directories(miniterminal PRIVATE
	"../../../../src/apps/terminal"
)
