set(SOURCES
    ShortcutsServerFilter.cpp
    KeyCommandMap.cpp
    CommandExecutor.cpp
    CommandActuators.cpp
    BitFieldTesters.cpp
    KeyInfos.cpp
    ParseCommandLine.cpp
)
add_library(shortcut_catcher SHARED ${SOURCES})
target_link_libraries(shortcut_catcher be)
UsePrivateHeaders(shortcut_catcher input interface)
set_target_properties(shortcut_catcher PROPERTIES PREFIX "")