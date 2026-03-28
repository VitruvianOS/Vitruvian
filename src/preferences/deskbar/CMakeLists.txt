Application(Deskbar_prefs
	SOURCES DeskbarPreferences.cpp
	LIBS localestub
	RDEF DeskbarPreferences.rdef
)
set_target_properties(Deskbar_prefs PROPERTIES OUTPUT_NAME Deskbar)
target_include_directories(Deskbar_prefs PRIVATE
	${PROJECT_SOURCE_DIR}/src/apps/deskbar)
