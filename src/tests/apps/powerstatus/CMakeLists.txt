# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

Test(
	test_sysfs_battery
	SOURCES
		test_sysfs_battery.cpp
		${PROJECT_SOURCE_DIR}/src/apps/powerstatus/SysFSDriverInterface.cpp
		${PROJECT_SOURCE_DIR}/src/apps/powerstatus/DriverInterface.cpp
	INCLUDES
		${PROJECT_SOURCE_DIR}/src/apps/powerstatus
		${PROJECT_SOURCE_DIR}/headers/private/device
)
