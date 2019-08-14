add_executable(DeskCalc
	CalcApplication.cpp
	CalcOptions.cpp
	CalcView.cpp
	CalcWindow.cpp
	DeskCalc.cpp
	ExpressionTextView.cpp
	InputTextView.cpp
)

target_link_libraries(DeskCalc PRIVATE be root)

target_include_directories(DeskCalc PRIVATE
	"../../../headers/private/interface/"
)
