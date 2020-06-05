
target_sources(be PRIVATE
	cat.cpp
	Catalog.cpp
	CatalogData.cpp
	Collator.cpp
	Country.cpp
	DefaultCatalog.cpp
	EditableCatalog.cpp
	FormattingConventions.cpp
	HashMapCatalog.cpp
	InitLocaleKit.cpp
	Language.cpp
	Locale.cpp
	LocaleRoster.cpp
	LocaleRosterData.cpp
	MutableLocaleRoster.cpp
	TextEncoding.cpp
	TimeZone.cpp
	DateFormat.cpp
	DateTimeFormat.cpp
	DurationFormat.cpp
	NumberFormat.cpp
	StringFormat.cpp
	TimeFormat.cpp
	TimeUnitFormat.cpp
	Format.cpp
	UnicodeChar.cpp
	RelativeDateTimeFormat.cpp
)


AddOn(
	localestub
	STATIC

	SOURCES
	CatalogStub.cpp
	LIBS icu
)

UsePrivateHeaders( localestub locale )
