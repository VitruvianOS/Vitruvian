AddOn(
	textencoding
	SHARED
	SOURCES
	CharacterSet.cpp
	CharacterSetRoster.cpp
	character_sets.cpp
	utf8_conversions.cpp
	LIBS localestub
)

UsePrivateHeaders( textencoding textencoding )
