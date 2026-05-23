add_library(switch_workspace SHARED SwitchWorkspaceInputFilter.cpp)
target_link_libraries(switch_workspace be)
UsePrivateHeaders(switch_workspace input app interface)
set_target_properties(switch_workspace PROPERTIES PREFIX "")