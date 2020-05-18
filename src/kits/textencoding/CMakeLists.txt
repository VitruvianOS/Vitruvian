target_sources(be PRIVATE
	CharacterSet.cpp
	CharacterSetRoster.cpp
	character_sets.cpp
	utf8_conversions.cpp
)

UsePrivateHeaders( be textencoding )
