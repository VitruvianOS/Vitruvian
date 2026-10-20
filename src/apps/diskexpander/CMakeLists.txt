Application(ExpandVolume
	SOURCES
	ExpandVolumeApp.cpp
	ExpandVolumeWindow.cpp
	LIBS shared localestub
	RDEF ExpandVolume.rdef
)
UsePrivateHeaders(ExpandVolume shared storage)

# ResizeJob/PartitionReference/DiskDeviceJobQueue live under the storage
# kit's disk_device/ tree, not headers/private/storage; the kit target adds
# them via its own target_include_directories(be ...). Same for DriveSetup.
target_include_directories(ExpandVolume PRIVATE
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device/jobs
)

# DoCatalogs last: with no catalogs shipped yet it return()s, and a
# macro-level return() ends this whole file.
DoCatalogs("application/x-vnd.vos-ExpandVolume" apps/diskexpander)
