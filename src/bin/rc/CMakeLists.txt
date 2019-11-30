find_package(BISON)
find_package(FLEX)

BISON_TARGET(parser parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cc)
FLEX_TARGET(lexer lexer.l  ${CMAKE_CURRENT_BINARY_DIR}/lexer.cc)

add_executable(rc
	${BISON_parser_OUTPUTS}
	${FLEX_lexer_OUTPUTS}
	compile.cpp
	decompile.cpp
	rdef.cpp
)


target_link_libraries(rc PRIVATE be root ${FLEX_LIBRARIES} ${BISON_LIBRARIES})

target_include_directories(rc PRIVATE
	"./"
	${CMAKE_CURRENT_BINARY_DIR}
)
