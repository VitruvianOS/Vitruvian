AddOn(BeControlLook SHARED
	SOURCES BeControlLook.cpp
	LIBS be
	RDEF BeControlLook.rdef
)
# The menu label is the file name.
set_target_properties(BeControlLook PROPERTIES PREFIX "" SUFFIX "")
UsePrivateHeaders(BeControlLook app interface shared)
