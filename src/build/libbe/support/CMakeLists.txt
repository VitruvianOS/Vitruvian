
set (SUPPORT_SOURCES ${KITS_SOURCES}/support/)

target_sources(be_build PRIVATE
	Archivable.cpp
	${SUPPORT_SOURCES}/BlockCache.cpp
	${SUPPORT_SOURCES}/BufferIO.cpp
	${SUPPORT_SOURCES}/ByteOrder.cpp
	${SUPPORT_SOURCES}/CompressionAlgorithm.cpp
	${SUPPORT_SOURCES}/DataIO.cpp
	${SUPPORT_SOURCES}/DataPositionIOWrapper.cpp
	${SUPPORT_SOURCES}/Flattenable.cpp
	${SUPPORT_SOURCES}/Job.cpp
	${SUPPORT_SOURCES}/JobQueue.cpp
	${SUPPORT_SOURCES}/List.cpp
	Locker.cpp
	${SUPPORT_SOURCES}/PointerList.cpp
	${SUPPORT_SOURCES}/Referenceable.cpp
	${SUPPORT_SOURCES}/String.cpp
	${SUPPORT_SOURCES}/StringList.cpp
	${SUPPORT_SOURCES}/Url.cpp
	${SUPPORT_SOURCES}/ZlibCompressionAlgorithm.cpp
	${SUPPORT_SOURCES}/ZstdCompressionAlgorithm.cpp
)

