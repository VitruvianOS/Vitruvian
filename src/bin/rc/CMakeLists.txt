find_package(BISON)
find_package(FLEX)

BISON_TARGET(parser parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp)
FLEX_TARGET(lexer lexer.l  ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)

Application(rc
	rc.cpp

	SOURCES
	${BISON_parser_OUTPUTS}
	${FLEX_lexer_OUTPUTS}
	compile.cpp
	decompile.cpp
	rdef.cpp

	LIBS
	${FLEX_LIBRARIES}
	${BISON_LIBRARIES}

	INCLUDES
	${CMAKE_CURRENT_BINARY_DIR}
)
