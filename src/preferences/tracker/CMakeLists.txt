Application(Tracker_prefs
	SOURCES TrackerPreferences.cpp
	LIBS localestub
	RDEF TrackerPreferences.rdef
)
set_target_properties(Tracker_prefs PROPERTIES OUTPUT_NAME Tracker)
