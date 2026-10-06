# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	SendToBluetooth SHARED

	SOURCES
	SendToBluetooth.cpp

	LIBS be bluetoothui tracker

	RDEF SendToBluetooth.rdef
)

target_include_directories(SendToBluetooth PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)
UsePrivateHeaders(SendToBluetooth tracker shared interface storage support)
