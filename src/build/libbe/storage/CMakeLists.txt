
set (STORAGE_SOURCES ${KITS_SOURCES}/storage/)

target_sources(be_build PRIVATE
	AppFileInfo.cpp
	Directory.cpp
	${STORAGE_SOURCES}/DriverSettings.cpp
	Entry.cpp
	${STORAGE_SOURCES}/EntryList.cpp
	${STORAGE_SOURCES}/FdIO.cpp
	File.cpp
	${STORAGE_SOURCES}/FileIO.cpp
	${STORAGE_SOURCES}/FindDirectory.cpp
	MergedDirectory.cpp
	Mime.cpp
	MimeType.cpp
	Node.cpp
	NodeInfo.cpp
	${STORAGE_SOURCES}/OffsetFile.cpp
	${STORAGE_SOURCES}/Path.cpp
	${STORAGE_SOURCES}/ResourceFile.cpp
	${STORAGE_SOURCES}/ResourceItem.cpp
	${STORAGE_SOURCES}/Resources.cpp
	${STORAGE_SOURCES}/ResourcesContainer.cpp
	${STORAGE_SOURCES}/ResourceStrings.cpp
	Statable.cpp
	${STORAGE_SOURCES}/SymLink.cpp
	Volume.cpp
	${STORAGE_SOURCES}/storage_support.cpp

	${STORAGE_SOURCES}/mime/AppMetaMimeCreator.cpp
	${STORAGE_SOURCES}/mime/AssociatedTypes.cpp
	${STORAGE_SOURCES}/mime/Database.cpp
	${STORAGE_SOURCES}/mime/DatabaseDirectory.cpp
	${STORAGE_SOURCES}/mime/DatabaseLocation.cpp
	${STORAGE_SOURCES}/mime/database_support.cpp
	${STORAGE_SOURCES}/mime/InstalledTypes.cpp
	${STORAGE_SOURCES}/mime/MimeEntryProcessor.cpp
	${STORAGE_SOURCES}/mime/MimeInfoUpdater.cpp
	${STORAGE_SOURCES}/mime/MimeSniffer.cpp
	${STORAGE_SOURCES}/mime/MimeSnifferAddon.cpp
	${STORAGE_SOURCES}/mime/MimeSnifferAddonManager.cpp
	${STORAGE_SOURCES}/mime/SnifferRules.cpp
	${STORAGE_SOURCES}/mime/Supertype.cpp
	${STORAGE_SOURCES}/mime/SupportingApps.cpp
	${STORAGE_SOURCES}/mime/TextSnifferAddon.cpp

	${STORAGE_SOURCES}/sniffer/CharStream.cpp
	${STORAGE_SOURCES}/sniffer/Err.cpp
	${STORAGE_SOURCES}/sniffer/DisjList.cpp
	${STORAGE_SOURCES}/sniffer/Pattern.cpp
	${STORAGE_SOURCES}/sniffer/PatternList.cpp
	${STORAGE_SOURCES}/sniffer/Parser.cpp
	${STORAGE_SOURCES}/sniffer/Range.cpp
	${STORAGE_SOURCES}/sniffer/RPattern.cpp
	${STORAGE_SOURCES}/sniffer/RPatternList.cpp
	${STORAGE_SOURCES}/sniffer/Rule.cpp
)

