add_library(minimize_all SHARED MinimizeAllInputFilter.cpp)
target_link_libraries(minimize_all be tracker)
UsePrivateHeaders(minimize_all input interface tracker)
set_target_properties(minimize_all PROPERTIES PREFIX "")