AddOn(FlatControlLook SHARED
	SOURCES FlatControlLook.cpp
	LIBS be
	RDEF FlatControlLook.rdef
)
# The menu label is the file name.
set_target_properties(FlatControlLook PROPERTIES PREFIX "" SUFFIX "")
UsePrivateHeaders(FlatControlLook app interface shared)
