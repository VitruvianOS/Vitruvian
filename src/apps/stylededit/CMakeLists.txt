add_executable(StyledEdit
	ColorMenuItem.cpp
	FindWindow.cpp
	ReplaceWindow.cpp
	StatusView.cpp
	StyledEditApp.cpp
	StyledEditView.cpp
	StyledEditWindow.cpp
)

target_link_libraries(StyledEdit PRIVATE be root tracker)

target_include_directories(StyledEdit PRIVATE
	"../../../headers/private/textencoding/"
)
