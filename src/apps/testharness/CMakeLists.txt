add_executable(testlist
	testlist.cpp
)

target_link_libraries(testlist PRIVATE be root)

add_executable(testoskit
	testoskit.cpp
)

target_link_libraries(testoskit PRIVATE be root)

add_executable(testports
	testports.cpp
)

target_link_libraries(testports PRIVATE be root)

add_executable(testsem
	testsem.cpp
)

target_link_libraries(testsem PRIVATE be root)

add_executable(testsempingpong
	testsempingpong.cpp
)

target_link_libraries(testsempingpong PRIVATE be root)

add_executable(testthread
	testthread.cpp
)

target_link_libraries(testthread PRIVATE be root)
