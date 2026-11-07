/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "hardware_info_parse.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>


struct cpuinfo_record {
	int			processor;
	char		model[96];
	char		vendor[32];
	double		mhz;
	int			implementer;	/* -1 = unknown */
	int			part;			/* -1 = unknown */
	char		uarch[64];
};


static int
read_trimmed(const char* path, char* buffer, size_t size)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return -1;

	size_t length = fread(buffer, 1, size - 1, file);
	fclose(file);
	buffer[length] = '\0';

	/* devicetree properties are NUL padded, not newline terminated */
	while (length > 0 && (buffer[length - 1] == '\0'
			|| buffer[length - 1] == '\n' || buffer[length - 1] == '\r'
			|| buffer[length - 1] == ' ' || buffer[length - 1] == '\t')) {
		buffer[--length] = '\0';
	}

	char* start = buffer;
	while (*start == ' ' || *start == '\t' || *start == '\n')
		start++;
	if (start != buffer)
		memmove(buffer, start, strlen(start) + 1);

	return (int)strlen(buffer);
}


static void
join_path(char* out, size_t size, const char* root, const char* relative)
{
	if (root == NULL || root[0] == '\0')
		snprintf(out, size, "%s", relative);
	else
		snprintf(out, size, "%s%s", root, relative);
}


static int
parse_cpu_list(const char* text, unsigned char* set, unsigned int setSize)
{
	memset(set, 0, setSize);

	const char* current = text;
	while (*current != '\0') {
		char* end = NULL;
		unsigned long first = strtoul(current, &end, 10);
		if (end == current)
			break;
		unsigned long last = first;
		current = end;
		if (*current == '-') {
			current++;
			last = strtoul(current, &end, 10);
			current = end;
		}
		for (unsigned long index = first;
				index <= last && index < setSize; index++) {
			set[index] = 1;
		}
		while (*current == ',' || *current == ' ' || *current == '\t'
				|| *current == '\n') {
			current++;
		}
	}
	return 0;
}


static void
trim_line(char* line)
{
	size_t length = strlen(line);
	while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'
			|| line[length - 1] == ' ' || line[length - 1] == '\t')) {
		line[--length] = '\0';
	}
}


static int
parse_cpuinfo(const char* root, struct cpuinfo_record* records,
	int maxRecords)
{
	char path[512];
	join_path(path, sizeof(path), root, "/proc/cpuinfo");

	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return 0;

	int count = 0;
	char line[512];
	struct cpuinfo_record current;
	memset(&current, 0, sizeof(current));
	current.processor = -1;
	current.implementer = -1;
	current.part = -1;

	for (;;) {
		if (fgets(line, sizeof(line), file) == NULL)
			break;
		trim_line(line);

		if (line[0] == '\0') {
			if (current.processor >= 0 && count < maxRecords)
				records[count++] = current;
			memset(&current, 0, sizeof(current));
			current.processor = -1;
			current.implementer = -1;
			current.part = -1;
			continue;
		}

		char* separator = strstr(line, ":");
		if (separator == NULL)
			continue;
		*separator = '\0';
		char* key = line;
		/* x86 aligns keys with tabs */
		for (char* end = key + strlen(key); end > key
				&& (end[-1] == '\t' || end[-1] == ' '); end--)
			end[-1] = '\0';
		char* value = separator + 1;
		while (*value == ' ' || *value == '\t')
			value++;
		trim_line(value);

		if (strcmp(key, "processor") == 0)
			current.processor = atoi(value);
		else if (strcmp(key, "model name") == 0)
			snprintf(current.model, sizeof(current.model), "%s", value);
		else if (strcmp(key, "vendor_id") == 0)
			snprintf(current.vendor, sizeof(current.vendor), "%s", value);
		else if (strcmp(key, "cpu MHz") == 0)
			current.mhz = strtod(value, NULL);
		else if (strcmp(key, "CPU implementer") == 0)
			current.implementer = (int)strtoul(value, NULL, 0);
		else if (strcmp(key, "CPU part") == 0)
			current.part = (int)strtoul(value, NULL, 0);
		else if (strcmp(key, "uarch") == 0)
			snprintf(current.uarch, sizeof(current.uarch), "%s", value);
	}

	if (current.processor >= 0 && count < maxRecords)
		records[count++] = current;

	fclose(file);
	return count;
}


static const char*
arm_part_name(int implementer, int part)
{
	if (implementer == 0x41) {
		switch (part) {
			case 0xb76: return "ARM1176";
			case 0xc05: return "Cortex-A5";
			case 0xc07: return "Cortex-A7";
			case 0xc08: return "Cortex-A8";
			case 0xc09: return "Cortex-A9";
			case 0xc0d: return "Cortex-A12";
			case 0xc0e: return "Cortex-A17";
			case 0xc0f: return "Cortex-A15";
			case 0xd03: return "Cortex-A53";
			case 0xd04: return "Cortex-A35";
			case 0xd05: return "Cortex-A55";
			case 0xd07: return "Cortex-A57";
			case 0xd08: return "Cortex-A72";
			case 0xd09: return "Cortex-A73";
			case 0xd0a: return "Cortex-A75";
			case 0xd0b: return "Cortex-A76";
			case 0xd0c: return "Neoverse-N1";
			case 0xd0d: return "Cortex-A77";
			case 0xd40: return "Neoverse-V1";
			case 0xd41: return "Cortex-A78";
			case 0xd44: return "Cortex-X1";
			case 0xd46: return "Cortex-A510";
			case 0xd47: return "Cortex-A710";
			case 0xd48: return "Cortex-X2";
			case 0xd49: return "Neoverse-N2";
			case 0xd4d: return "Cortex-A715";
			case 0xd4e: return "Cortex-X3";
			case 0xd80: return "Cortex-A520";
			case 0xd81: return "Cortex-A720";
			case 0xd82: return "Cortex-X4";
		}
		return "ARM CPU";
	}
	if (implementer == 0x51)
		return "Qualcomm CPU";
	if (implementer == 0x61)
		return "Apple CPU";
	if (implementer == 0x48)
		return "HiSilicon CPU";
	if (implementer == 0x4e)
		return "NVIDIA CPU";
	if (part >= 0)
		return "ARM CPU";
	return NULL;
}


static void
format_riscv_uarch(const char* uarch, char* out, size_t size)
{
	char vendor[32];
	char core[32];
	vendor[0] = '\0';
	core[0] = '\0';

	const char* comma = strchr(uarch, ',');
	if (comma != NULL) {
		size_t vendorLength = (size_t)(comma - uarch);
		if (vendorLength >= sizeof(vendor))
			vendorLength = sizeof(vendor) - 1;
		memcpy(vendor, uarch, vendorLength);
		vendor[vendorLength] = '\0';
		snprintf(core, sizeof(core), "%s", comma + 1);
	} else {
		snprintf(core, sizeof(core), "%s", uarch);
	}

	if (strcasecmp(vendor, "sifive") == 0)
		snprintf(vendor, sizeof(vendor), "SiFive");
	else if (strcasecmp(vendor, "thead") == 0)
		snprintf(vendor, sizeof(vendor), "T-Head");
	else if (vendor[0] != '\0')
		vendor[0] = (char)toupper((unsigned char)vendor[0]);

	/* short tokens are core names, u74 becomes U74 */
	if (core[0] != '\0' && strlen(core) <= 4) {
		for (char* c = core; *c != '\0'; c++)
			*c = (char)toupper((unsigned char)*c);
	}

	if (vendor[0] != '\0' && core[0] != '\0')
		snprintf(out, size, "%s %s", vendor, core);
	else if (core[0] != '\0')
		snprintf(out, size, "%s", core);
	else
		snprintf(out, size, "%s", uarch);
}


static void
clean_x86_model(const char* model, char* out, size_t size)
{
	size_t outIndex = 0;
	for (size_t i = 0; model[i] != '\0' && outIndex + 4 < size; i++) {
		if (strncmp(&model[i], "(R)", 3) == 0) {
			out[outIndex++] = '\xc2';
			out[outIndex++] = '\xae';
			i += 2;
		} else if (strncmp(&model[i], "(TM)", 4) == 0) {
			out[outIndex++] = '\xe2';
			out[outIndex++] = '\x84';
			out[outIndex++] = '\xa2';
			i += 3;
		} else if (strncmp(&model[i], " CPU", 4) == 0) {
			i += 3;
		} else if (strncmp(&model[i], " @", 2) == 0) {
			break;
		} else if (model[i] == '\n') {
			break;
		} else
			out[outIndex++] = model[i];
	}
	out[outIndex] = '\0';

	size_t length = strlen(out);
	while (length > 0 && out[length - 1] == ' ')
		out[--length] = '\0';
}


static unsigned long long
frequency_from_model(const char* model)
{
	const char* at = strstr(model, "@ ");
	if (at == NULL)
		return 0;
	char* end = NULL;
	double value = strtod(at + 2, &end);
	if (value <= 0.0 || end == NULL)
		return 0;
	if (*end == 'G' || *end == 'g')
		return (unsigned long long)(value * 1000000000.0);
	if (*end == 'M' || *end == 'm')
		return (unsigned long long)(value * 1000000.0);
	return 0;
}


static void
parse_machine_name(const char* root, struct vos_hw_info* out)
{
	char path[512];
	char buffer[VOS_HW_NAME_LENGTH];

	join_path(path, sizeof(path), root,
		"/sys/firmware/devicetree/base/model");
	if (read_trimmed(path, buffer, sizeof(buffer)) > 0) {
		snprintf(out->machine_name, sizeof(out->machine_name), "%s", buffer);
		return;
	}

	char vendor[VOS_HW_NAME_LENGTH];
	char product[VOS_HW_NAME_LENGTH];
	char board[VOS_HW_NAME_LENGTH];
	vendor[0] = '\0';
	product[0] = '\0';
	board[0] = '\0';

	join_path(path, sizeof(path), root, "/sys/class/dmi/id/sys_vendor");
	read_trimmed(path, vendor, sizeof(vendor));
	join_path(path, sizeof(path), root, "/sys/class/dmi/id/product_name");
	read_trimmed(path, product, sizeof(product));
	join_path(path, sizeof(path), root, "/sys/class/dmi/id/board_name");
	read_trimmed(path, board, sizeof(board));

	const char* name = product[0] != '\0' ? product : board;
	if (name[0] == '\0') {
		if (vendor[0] != '\0')
			snprintf(out->machine_name, sizeof(out->machine_name), "%s",
				vendor);
		return;
	}

	if (vendor[0] != '\0' && strncmp(name, vendor, strlen(vendor)) != 0)
		snprintf(out->machine_name, sizeof(out->machine_name), "%.60s %.60s",
			vendor, name);
	else
		snprintf(out->machine_name, sizeof(out->machine_name), "%s", name);
}


static void
parse_soc(const char* root, struct vos_hw_info* out)
{
	char path[512];
	char buffer[VOS_HW_NAME_LENGTH];

	join_path(path, sizeof(path), root, "/sys/devices/soc0/family");
	if (read_trimmed(path, buffer, sizeof(buffer)) <= 0) {
		join_path(path, sizeof(path), root, "/sys/devices/soc0/machine");
		if (read_trimmed(path, buffer, sizeof(buffer)) <= 0)
			return;
	}
	snprintf(out->soc_name, sizeof(out->soc_name), "%s", buffer);
}


static bool
is_bus_driver(const char* name)
{
	/* PCI bus drivers sit between the card and the real DRM driver */
	return strcmp(name, "virtio-pci") == 0
		|| strcmp(name, "pcieport") == 0
		|| strcmp(name, "pci_bus") == 0
		|| strcmp(name, "root_port") == 0;
}


static bool
is_display_driver(const char* name)
{
	/* the GPU's virtio parent also hosts net and block devices */
	if (strncmp(name, "virtio_", 7) == 0)
		return strcmp(name, "virtio_gpu") == 0;
	return !is_bus_driver(name);
}


static int
read_driver_name(const char* devicePath, char* out, size_t size)
{
	char path[600];
	char linkTarget[512];

	snprintf(path, sizeof(path), "%.520s/driver", devicePath);
	ssize_t length = readlink(path, linkTarget, sizeof(linkTarget) - 1);
	if (length < 0)
		return -1;
	linkTarget[length] = '\0';

	const char* base = strrchr(linkTarget, '/');
	base = base != NULL ? base + 1 : linkTarget;
	if (base[0] == '\0' || strcmp(base, "drm") == 0)
		return -1;
	snprintf(out, size, "%s", base);
	return 0;
}


static void
parse_gpu(const char* root, struct vos_hw_info* out)
{
	char devicePath[560];
	char name[VOS_HW_NAME_LENGTH];

	for (int card = 0; card < 16; card++) {
		snprintf(devicePath, sizeof(devicePath),
			"%s/sys/class/drm/card%d/device", root == NULL ? "" : root, card);

		if (read_driver_name(devicePath, name, sizeof(name)) != 0)
			continue;

		if (is_bus_driver(name)) {
			/* e.g. virtio-gpu lives one level below virtio-pci */
			DIR* directory = opendir(devicePath);
			if (directory != NULL) {
				char childName[VOS_HW_NAME_LENGTH];
				struct dirent* entry;
				while ((entry = readdir(directory)) != NULL) {
					if (entry->d_name[0] == '.')
						continue;
					char child[640];
					snprintf(child, sizeof(child), "%.520s/%.100s",
						devicePath, entry->d_name);
					if (read_driver_name(child, childName,
							sizeof(childName)) == 0
							&& is_display_driver(childName)) {
						snprintf(name, sizeof(name), "%s",
							childName);
						break;
					}
				}
				closedir(directory);
			}
		}

		for (char* c = name; *c != '\0'; c++) {
			if (*c == '_')
				*c = '-';
		}
		snprintf(out->gpu_name, sizeof(out->gpu_name), "%.120s", name);
		return;
	}
}


static void
parse_memory(const char* root, struct vos_hw_info* out)
{
	char path[512];
	join_path(path, sizeof(path), root, "/proc/meminfo");

	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return;

	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, "MemTotal:", 9) == 0)
			out->memory_total = strtoull(line + 9, NULL, 10) * 1024ULL;
		else if (strncmp(line, "MemAvailable:", 13) == 0)
			out->memory_usable = strtoull(line + 13, NULL, 10) * 1024ULL;
	}
	fclose(file);
}

static void
parse_cpus(const char* root, const struct cpuinfo_record* records,
	int recordCount, struct vos_hw_info* out)
{
	char path[512];

	/* the kernel names the two Intel hybrid core classes itself */
	unsigned char coreSet[VOS_HW_MAX_CPUS];
	unsigned char atomSet[VOS_HW_MAX_CPUS];
	bool hybrid = false;
	char listBuffer[1024];
	join_path(path, sizeof(path), root, "/sys/devices/cpu_core/cpus");
	if (read_trimmed(path, listBuffer, sizeof(listBuffer)) > 0) {
		parse_cpu_list(listBuffer, coreSet, sizeof(coreSet));
		join_path(path, sizeof(path), root, "/sys/devices/cpu_atom/cpus");
		if (read_trimmed(path, listBuffer, sizeof(listBuffer)) > 0) {
			parse_cpu_list(listBuffer, atomSet, sizeof(atomSet));
			hybrid = true;
		}
	}

	unsigned char used[VOS_HW_MAX_CPUS];
	memset(used, 0, sizeof(used));

	join_path(path, sizeof(path), root, "/sys/devices/system/cpu");
	DIR* directory = opendir(path);
	if (directory == NULL)
		return;

	/* readdir order is not numeric */
	int cpuIndices[VOS_HW_MAX_CPUS];
	int cpuCount = 0;
	struct dirent* entry;
	while ((entry = readdir(directory)) != NULL && cpuCount < VOS_HW_MAX_CPUS) {
		const char* name = entry->d_name;
		if (strncmp(name, "cpu", 3) != 0)
			continue;
		const char* digits = name + 3;
		if (*digits == '\0')
			continue;
		bool allDigits = true;
		for (const char* c = digits; *c != '\0'; c++) {
			if (!isdigit((unsigned char)*c)) {
				allDigits = false;
				break;
			}
		}
		if (!allDigits)
			continue;
		cpuIndices[cpuCount++] = atoi(digits);
	}
	closedir(directory);

	for (int i = 0; i + 1 < cpuCount; i++) {
		for (int j = i + 1; j < cpuCount; j++) {
			if (cpuIndices[j] < cpuIndices[i]) {
				int swap = cpuIndices[i];
				cpuIndices[i] = cpuIndices[j];
				cpuIndices[j] = swap;
			}
		}
	}

	const struct cpuinfo_record* groupRecords[VOS_HW_MAX_GROUPS];
	int groupClasses[VOS_HW_MAX_GROUPS];
	memset(groupRecords, 0, sizeof(groupRecords));
	memset(groupClasses, 0, sizeof(groupClasses));

	for (int n = 0; n < cpuCount; n++) {
		int cpu = cpuIndices[n];
		if (cpu >= VOS_HW_MAX_CPUS || used[cpu])
			continue;

		char cpuPath[560];
		snprintf(cpuPath, sizeof(cpuPath), "%.500s/cpu%d", path, cpu);

		char onlinePath[560];
		snprintf(onlinePath, sizeof(onlinePath), "%.520s/online", cpuPath);
		char onlineBuffer[16];
		if (read_trimmed(onlinePath, onlineBuffer, sizeof(onlineBuffer)) > 0
				&& onlineBuffer[0] == '0') {
			continue;
		}

		const struct cpuinfo_record* record = NULL;
		for (int i = 0; i < recordCount; i++) {
			if (records[i].processor == cpu) {
				record = &records[i];
				break;
			}
		}

		char key[112];
		int groupClass = 0;	/* 1 = P-core, 2 = E-core */
		if (hybrid) {
			groupClass = atomSet[cpu] ? 2 : 1;
			snprintf(key, sizeof(key), "hybrid%d", groupClass);
		} else {
			char capacityPath[560];
			snprintf(capacityPath, sizeof(capacityPath), "%.520s/cpu_capacity",
				cpuPath);
			char capacityBuffer[32];
			int capacity = -1;
			if (read_trimmed(capacityPath, capacityBuffer,
					sizeof(capacityBuffer)) > 0) {
				capacity = atoi(capacityBuffer);
			}

			/* capacity splits big.LITTLE twins of the same part */
			if (record != NULL && record->part >= 0)
				snprintf(key, sizeof(key), "part%x_%d", record->part,
					capacity >= 700 ? 1 : 0);
			else if (record != NULL && record->uarch[0] != '\0')
				snprintf(key, sizeof(key), "uarch:%s", record->uarch);
			else if (record != NULL && record->model[0] != '\0')
				snprintf(key, sizeof(key), "model:%s_%d", record->model,
					capacity >= 700 ? 1 : 0);
			else if (capacity >= 0)
				snprintf(key, sizeof(key), "cap%d", capacity >= 700 ? 1 : 0);
			else
				snprintf(key, sizeof(key), "cpu");
		}

		unsigned long long frequency = 0;
		char freqPath[560];
		snprintf(freqPath, sizeof(freqPath), "%.520s/cpufreq/cpuinfo_max_freq",
			cpuPath);
		char freqBuffer[32];
		if (read_trimmed(freqPath, freqBuffer, sizeof(freqBuffer)) > 0) {
			unsigned long long khz = strtoull(freqBuffer, NULL, 10);
			if (khz > 0)
				frequency = khz * 1000ULL;
		}
		if (frequency == 0 && record != NULL && record->mhz > 0.0)
			frequency = (unsigned long long)(record->mhz * 1000000.0);
		if (frequency == 0 && record != NULL)
			frequency = frequency_from_model(record->model);

		int groupIndex = -1;
		for (unsigned int i = 0; i < out->group_count; i++) {
			if (groupClasses[i] != groupClass)
				continue;
			const char* parked = out->groups[i].name;
			if (parked[0] == '#')
				parked++;
			if (strcmp(parked, key) == 0) {
				groupIndex = (int)i;
				break;
			}
		}

		if (groupIndex < 0) {
			if (out->group_count >= VOS_HW_MAX_GROUPS)
				continue;
			groupIndex = (int)out->group_count++;
			vos_hw_cpu_group* group = &out->groups[groupIndex];
			memset(group, 0, sizeof(*group));
			/* '#' parks the raw key until naming resolves it */
			snprintf(group->name, sizeof(group->name), "#%s", key);
			group->max_frequency = frequency;
			groupClasses[groupIndex] = groupClass;
			groupRecords[groupIndex] = record;
		}

		vos_hw_cpu_group* group = &out->groups[groupIndex];
		group->cpu_count++;
		if (frequency > group->max_frequency)
			group->max_frequency = frequency;

		used[cpu] = 1;
	}

	for (unsigned int i = 0; i < out->group_count; i++) {
		vos_hw_cpu_group* group = &out->groups[i];
		const struct cpuinfo_record* record = groupRecords[i];
		char model[VOS_HW_NAME_LENGTH];
		model[0] = '\0';
		if (record != NULL && record->model[0] != '\0')
			clean_x86_model(record->model, model, sizeof(model));

		if (groupClasses[i] == 1 || groupClasses[i] == 2) {
			if (model[0] == '\0')
				snprintf(model, sizeof(model), "x86 CPU");
			snprintf(group->name, sizeof(group->name), "%.110s (%s)", model,
				groupClasses[i] == 2 ? "E-core" : "P-core");
			continue;
		}

		const char* name = NULL;
		char formatted[VOS_HW_NAME_LENGTH];
		formatted[0] = '\0';
		if (record != NULL && record->part >= 0)
			name = arm_part_name(record->implementer, record->part);
		if (name == NULL && record != NULL && record->uarch[0] != '\0') {
			format_riscv_uarch(record->uarch, formatted, sizeof(formatted));
			name = formatted;
		}
		if (name == NULL && model[0] != '\0')
			name = model;
		if (name == NULL || name[0] == '\0')
			name = "CPU";
		snprintf(group->name, sizeof(group->name), "%s", name);
	}
}


int
vos_hw_parse(const char* root, struct vos_hw_info* out)
{
	if (out == NULL)
		return -1;

	memset(out, 0, sizeof(*out));
	if (root == NULL)
		root = "";

	struct cpuinfo_record records[VOS_HW_MAX_CPUS];
	int recordCount = parse_cpuinfo(root, records, VOS_HW_MAX_CPUS);

	parse_cpus(root, records, recordCount, out);
	parse_machine_name(root, out);
	parse_soc(root, out);
	parse_gpu(root, out);
	parse_memory(root, out);

	return 0;
}
