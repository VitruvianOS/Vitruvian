AddOn(
	textencoding
	SHARED
	SOURCES
	CharacterSet.cpp
	CharacterSetRoster.cpp
	character_sets.cpp
	utf8_conversions.cpp
	LIBS be localestub

	RDEF libtextencoding.rdef
)

UsePrivateHeaders( textencoding textencoding )
DoCatalogs("x-vnd.vos-libtextencoding" kits/textencoding)
