Application(
	DeskCalc

	SOURCES
	CalcApplication.cpp
	CalcOptions.cpp
	CalcView.cpp
	CalcWindow.cpp
	DeskCalc.cpp
	ExpressionTextView.cpp
	InputTextView.cpp
	${PROJECT_SOURCE_DIR}/src/kits/shared/ExpressionParser.cpp
	${PROJECT_SOURCE_DIR}/src/kits/locale/CatalogStub.cpp
	RDEF
	DeskCalc.rdef
	LIBS
	mapm
)

UsePrivateHeaders( DeskCalc interface shared )
target_include_directories(DeskCalc PRIVATE ${PROJECT_SOURCE_DIR}/src/libs/mapm)
