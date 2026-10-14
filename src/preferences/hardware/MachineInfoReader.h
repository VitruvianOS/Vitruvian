/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MACHINE_INFO_READER_H
#define _MACHINE_INFO_READER_H


#include <string>

#include "HardwareModel.h"


// Summary-header data (DMI or DT model, CPU, memory, firmware, kernel).
// sysRoot and procRoot are overridable so the host tests can point it
// at a captured tree; the preflet passes /sys and /proc.


namespace Hardware {


MachineInfo
ReadMachineInfo(const std::string& sysRoot, const std::string& procRoot);


}	// namespace Hardware


#endif	// _MACHINE_INFO_READER_H
