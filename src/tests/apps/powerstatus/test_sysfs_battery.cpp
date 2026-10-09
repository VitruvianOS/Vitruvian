/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Host-side SysFSDriverInterface tests against fake power_supply trees.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "SysFSDriverInterface.h"
#include "DriverInterface.h"

static int g_failures = 0;

static void
expect(int condition, const char* what)
{
	if (condition) {
		printf("ok   %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		g_failures++;
	}
}

static int
write_file(const char* path, const char* contents)
{
	FILE* f = fopen(path, "w");
	if (f == NULL)
		return -1;
	fputs(contents, f);
	fclose(f);
	return 0;
}

static int
make_dir(const char* path)
{
	// Create parents first so nested fake sysfs trees work.
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char* p = tmp + 1; *p != '\0'; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}
	return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static void
rm_rf(const char* path)
{
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	int rc = system(cmd);
	(void)rc;
}

static int
write_attr(const char* dir, const char* name, const char* contents)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return write_file(path, contents);
}

static void
make_battery(const char* root, const char* name, const char* const* attrs,
	const char* const* values)
{
	char dir[512];
	snprintf(dir, sizeof(dir), "%s/%s", root, name);
	make_dir(dir);
	for (int i = 0; attrs[i] != NULL; i++)
		write_attr(dir, attrs[i], values[i]);
}

static double
percent_of(const battery_info& info)
{
	if (info.full_capacity <= 0)
		return -1.0;
	return (double)info.capacity / (double)info.full_capacity;
}


static void
test_capacity_only(void)
{
	const char* root = "test-sysfs-capacity-only";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "capacity", NULL };
	const char* values[] = { "Battery", "Discharging", "75\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	status_t connectStatus = driver.Connect();
	printf("     connect(%s) = %d\n", root, connectStatus);
	expect(connectStatus == B_OK, "capacity-only Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "capacity-only read");
	double percent = percent_of(info);
	printf("     capacity-only: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.74 && percent <= 0.76, "capacity-only is ~75%");
	expect(info.state == BATTERY_DISCHARGING, "capacity-only discharging");
}


static void
test_energy_only(void)
{
	const char* root = "test-sysfs-energy-only";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "energy_now", "energy_full",
		NULL };
	const char* values[] = { "Battery", "Discharging", "36000000\n",
		"48000000\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "energy-only Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "energy-only read");
	double percent = percent_of(info);
	printf("     energy-only: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.74 && percent <= 0.76, "energy-only is ~75%");
	expect(info.full_capacity == 48000, "energy full is 48000 mWh");
}


static void
test_charge_only(void)
{
	const char* root = "test-sysfs-charge-only";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "charge_now", "charge_full",
		NULL };
	const char* values[] = { "Battery", "Discharging", "2400000\n",
		"3200000\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "charge-only Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "charge-only read");
	double percent = percent_of(info);
	printf("     charge-only: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.74 && percent <= 0.76, "charge-only is ~75%");
	expect(info.full_capacity == 3200, "charge full is 3200 mAh");
}


static void
test_capacity_missing_energy_present(void)
{
	const char* root = "test-sysfs-capacity-missing";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "energy_now", "energy_full",
		NULL };
	const char* values[] = { "Battery", "Charging", "12000000\n",
		"48000000\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "capacity-missing Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "capacity-missing read");
	double percent = percent_of(info);
	printf("     capacity-missing: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.24 && percent <= 0.26, "capacity-missing is ~25%");
	expect(info.state == BATTERY_CHARGING, "capacity-missing charging");
}


static void
test_capacity_zero_with_energy(void)
{
	// ThinkPad X130e style: EC reports capacity=0, energy_* is valid.
	const char* root = "test-sysfs-capacity-zero";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "capacity", "energy_now",
		"energy_full", NULL };
	const char* values[] = { "Battery", "Discharging", "0\n", "36000000\n",
		"48000000\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "capacity-zero Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "capacity-zero read");
	double percent = percent_of(info);
	printf("     capacity-zero: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.74 && percent <= 0.76,
		"capacity-zero falls back to energy (~75%)");
}


static void
test_percent_with_energy_full(void)
{
	// Regression: percent capacity must not be divided by mWh full.
	const char* root = "test-sysfs-percent-vs-mwh";
	rm_rf(root);
	const char* attrs[] = { "type", "status", "capacity", "energy_full",
		NULL };
	const char* values[] = { "Battery", "Discharging", "50\n", "48000000\n",
		NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "percent-vs-mWh Connect");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "percent-vs-mWh read");
	double percent = percent_of(info);
	printf("     percent-vs-mWh: capacity=%d full=%d percent=%.3f\n",
		info.capacity, info.full_capacity, percent);
	expect(percent >= 0.49 && percent <= 0.51,
		"percent capacity yields ~50%, not ~0%");
}


static void
test_ac_adapter_ignored(void)
{
	const char* root = "test-sysfs-ac-ignored";
	rm_rf(root);
	const char* acAttrs[] = { "type", "online", NULL };
	const char* acValues[] = { "Mains", "1\n", NULL };
	make_battery(root, "AC", acAttrs, acValues);
	const char* attrs[] = { "type", "status", "capacity", NULL };
	const char* values[] = { "Battery", "Full", "100\n", NULL };
	make_battery(root, "BAT0", attrs, values);

	SysFSDriverInterface driver(root);
	expect(driver.Connect() == B_OK, "ac-ignored Connect");
	expect(driver.GetBatteryCount() == 1, "AC adapter is not a battery");
	battery_info info;
	memset(&info, 0, sizeof(info));
	expect(driver.GetBatteryInfo(0, &info) == B_OK, "ac-ignored read");
	expect(info.state == 0, "full battery is not critical");
}


int
main()
{
	test_capacity_only();
	test_energy_only();
	test_charge_only();
	test_capacity_missing_energy_present();
	test_capacity_zero_with_energy();
	test_percent_with_energy_full();
	test_ac_adapter_ignored();

	if (g_failures == 0) {
		printf("ALL PASSED\n");
		return 0;
	}
	printf("%d FAILURES\n", g_failures);
	return 1;
}
