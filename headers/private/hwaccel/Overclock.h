/*
 * Copyright 2026, Vitruvian OS contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OVERCLOCK_H
#define _OVERCLOCK_H


#include <SupportDefs.h>

#include <string>
#include <vector>


//! CPU frequency scaling state, read from Linux cpufreq sysfs.
struct CpufreqState {
	std::string	driver;				// scaling_driver, empty if absent
	std::string	governor;			// scaling_governor
	std::vector<std::string> governors;	// scaling_available_governors
	int64		curKHz = -1;		// scaling_cur_freq
	int64		minKHz = -1;		// scaling_min_freq (writable)
	int64		maxKHz = -1;		// scaling_max_freq (writable)
	int64		hwMinKHz = -1;		// cpuinfo_min_freq (hardware limit)
	int64		hwMaxKHz = -1;		// cpuinfo_max_freq (hardware limit)
	bool		boostAvailable = false;
	bool		boostOn = false;
	std::vector<std::string> eppPreferences;	// empty unless EPP exists
	std::string	epp;				// energy_performance_preference
};


//! Intel RAPL power zone (powercap sysfs), power in microwatts.
struct RaplState {
	bool		available = false;
	std::string	name;				// "package-0" and friends
	int64		curLongUw = -1;		// constraint_0_power_limit_uw
	int64		maxLongUw = -1;		// constraint_0_max_power_uw
	int64		curShortUw = -1;	// constraint_1_power_limit_uw; -1: no short term
	int64		maxShortUw = -1;
};


//! amd_pstate driver mode and preferred-core switch (read only).
struct AmdPstateState {
	bool		available = false;
	std::string	status;				// active / passive / guided / disabled
	bool		prefCoreEnabled = false;
	bool		prefCoreAvailable = false;
};


//! One DIMM decoded from its SPD EEPROM (ee1004 / spd5118 eeprom attr).
struct DimmState {
	int			slot = -1;			// SMBus address index, 0x50 + slot
	std::string	type;				// "DDR3", "DDR4", "DDR5", "unknown"
	long long	sizeMB = -1;
	int			maxSpeedMTs = -1;	// from tCKAVGmin
	bool		xmp = false;		// XMP/EXPO signature present
	int			xmpProfiles = 0;
	bool		decoded = false;	// false: present but not understood
};


//! Raspberry Pi config.txt overclock keys (apply at next boot).
struct RpiOverclockState {
	bool		hasArmFreq = false;
	int			armFreq = -1;		// MHz
	bool		hasOverVoltage = false;
	int			overVoltage = 0;	// firmware steps, not millivolts
	bool		hasSdramFreq = false;
	int			sdramFreq = -1;		// MHz
	bool		hasGpuFreq = false;
	int			gpuFreq = -1;		// MHz
};


/*!
	 * Backend for the Overclocking tab; one implementation is probed per platform.
	 * Writes stay within the kernel-reported limits and run as root (pkexec helper).
	 */
class OverclockBackend {
public:
	static	OverclockBackend*	Create(const char* sysfsRoot = NULL,
									const char* bootRoot = NULL);

	virtual						~OverclockBackend();

	virtual	const char*			Name() const = 0;

	//! True: writes land in config.txt and take effect at the next boot.
	virtual	bool				IsBootConfig() const = 0;

	virtual	bool				GetCpu(CpufreqState& state) = 0;
	virtual	status_t			SetGovernor(const char* governor) = 0;
	virtual	status_t			SetFreqRange(int64 minKHz,
									int64 maxKHz) = 0;
	virtual	status_t			SetBoost(bool enable) = 0;

	virtual	bool				GetRapl(RaplState& state);
	virtual	status_t			SetPowerLimit(int64 longUw,
									int64 shortUw);

	virtual	bool				GetAmdPstate(AmdPstateState& state);
	virtual	status_t			SetEpp(const char* preference);

	virtual	bool				GetRpi(RpiOverclockState& state);
	virtual	status_t			SetRpi(const RpiOverclockState& state);

	//! SPD EEPROMs on the SMBus; empty when none are readable.
	virtual	bool				GetDimms(std::vector<DimmState>& dimms);

	//! Decode one raw SPD dump (DDR3/DDR4 basics plus XMP detection).
	static	status_t			DecodeSpd(const uint8* data, size_t length,
									DimmState& dimm);

protected:
								OverclockBackend();
};


#endif	// _OVERCLOCK_H
