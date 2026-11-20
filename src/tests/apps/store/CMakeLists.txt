# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

# Store page template, driven against a DOM stub (needs node on the host).
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
	add_test(NAME store_page
		COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/page_test.js)
endif()
