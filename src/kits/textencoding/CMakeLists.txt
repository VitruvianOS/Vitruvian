target_sources(be PRIVATE
	CharacterSet.cpp
	CharacterSetRoster.cpp
	character_sets.cpp
	utf8_conversions.cpp
)

target_include_directories(be PRIVATE
	"../../../headers/private/textencoding/"
)
