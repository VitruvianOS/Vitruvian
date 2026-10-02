# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	SendToBluetooth SHARED

	SOURCES
	SendToBluetooth.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexSendChooser.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexTransferWindow.cpp

	LIBS be tracker

	RDEF SendToBluetooth.rdef
)

target_include_directories(SendToBluetooth PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/libbluetooth
)
UsePrivateHeaders(SendToBluetooth tracker shared interface storage support)
