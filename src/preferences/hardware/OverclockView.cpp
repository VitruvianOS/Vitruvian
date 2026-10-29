/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "OverclockView.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <StringView.h>
#include <TextControl.h>

#include "OverclockTemperatures.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


static const char* kHelper = "/usr/libexec/vos-set-overclock";

static const uint32 kMsgApplyNow = 'ocAn';
static const uint32 kMsgApplyBoot = 'ocAb';
static const uint32 kMsgRevert = 'ocRv';
static const uint32 kMsgHelperDone = 'ocHd';
static const uint32 kMsgHelperOutput = 'ocHo';
static const uint32 kMsgTick = 'ocTk';

extern char** environ;


static int64
_LabelToKHz(const char* label)
{
	// Labels are "<int> MHz".
	return (int64)(strtod(label, NULL) * 1000.0);
}


static void
_FmtMHz(char* buffer, size_t size, int64 khz)
{
	snprintf(buffer, size, "%d MHz", (int)((khz + 500) / 1000));
}


static status_t
_HexDecode(const char* text, uint8* out, size_t outSize)
{
	size_t length = strlen(text);
	if (length % 2 != 0 || length / 2 > outSize)
		return B_BAD_VALUE;

	for (size_t i = 0; i < length; i += 2) {
		unsigned int byte = 0;
		if (sscanf(text + i, "%2x", &byte) != 1)
			return B_BAD_VALUE;
		out[i / 2] = (uint8)byte;
	}
	return B_OK;
}


static status_t
_HelperThread(void* data)
{
	OverclockHelperJob* job = (OverclockHelperJob*)data;

	int outputPipe[2] = { -1, -1 };
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);

	if (job->spdRequest && pipe(outputPipe) == 0) {
		posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
		posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
		posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
	}

	// posix_spawn, not fork(): no fork handlers run in this
	// multithreaded process for a child that only execs.
	int32 count = job->arguments.CountStrings();
	char** argv = new char*[count + 3];
	argv[0] = (char*)"pkexec";
	argv[1] = (char*)kHelper;
	for (int32 i = 0; i < count; i++)
		argv[i + 2] = strdup(job->arguments.StringAt(i).String());
	argv[count + 2] = NULL;

	int status = -1;
	pid_t pid = -1;
	if (posix_spawnp(&pid, "pkexec", &actions, NULL, argv, environ) == 0) {
		if (job->spdRequest && outputPipe[0] >= 0) {
			close(outputPipe[1]);
			outputPipe[1] = -1;
		}
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}
	posix_spawn_file_actions_destroy(&actions);

	BString output;
	if (job->spdRequest && outputPipe[0] >= 0) {
		char buffer[512];
		ssize_t bytes;
		while ((bytes = read(outputPipe[0], buffer, sizeof(buffer)))
				> 0) {
			output.Append(buffer, bytes);
		}
		close(outputPipe[0]);
	}

	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		fprintf(stderr, "Hardware: %s failed\n", kHelper);

	if (job->spdRequest) {
		BMessage message(kMsgHelperOutput);
		message.AddBool("ok", ok);
		message.AddString("output", output);
		job->target.SendMessage(&message);
	} else {
		BMessage done(kMsgHelperDone);
		job->target.SendMessage(&done);
	}

	for (int32 i = 0; i < count; i++)
		free(argv[i + 2]);
	delete[] argv;
	delete job;
	return B_OK;
}


OverclockView::OverclockView()
	:
	BView("overclocking", B_WILL_DRAW),
	fBackend(NULL),
	fTimer(NULL),
	fBusy(false),
	fSpdLoaded(false),
	fDriverString(NULL),
	fGovernorMenu(NULL),
	fCurrentFreqString(NULL),
	fMinFreqMenu(NULL),
	fMaxFreqMenu(NULL),
	fBoostCheckBox(NULL),
	fEppMenu(NULL),
	fPowerLongText(NULL),
	fPowerShortText(NULL),
	fPowerMaxString(NULL),
	fCpuTempString(NULL),
	fArmFreqMenu(NULL),
	fOverVoltageText(NULL),
	fSdramFreqMenu(NULL),
	fGpuFreqMenu(NULL),
	fRpiNote(NULL),
	fMemoryNote(NULL),
	fRamTempString(NULL),
	fGpuBox(NULL),
	fStatusString(NULL)
{
	for (int i = 0; i < 4; i++) {
		char name[16];
		snprintf(name, sizeof(name), "dimm%d", i);
		fDimmStrings[i] = new BStringView(name, "");
		fDimmStrings[i]->Hide();
	}

	fBackend = OverclockBackend::Create();

	fDriverString = new BStringView("driver", "");
	fCurrentFreqString = new BStringView("currentfreq", "");
	fCpuTempString = new BStringView("cputemp", "");
	fPowerMaxString = new BStringView("powermax", "");
	fStatusString = new BStringView("status", "");

	fGovernorMenu = new BMenuField("governor", B_TRANSLATE("Governor:"),
		new BMenu("governor"));
	fMinFreqMenu = new BMenuField("minfreq", B_TRANSLATE("Minimum:"),
		new BMenu("minfreq"));
	fMaxFreqMenu = new BMenuField("maxfreq", B_TRANSLATE("Maximum:"),
		new BMenu("maxfreq"));
	fEppMenu = new BMenuField("epp", B_TRANSLATE("Energy preference:"),
		new BMenu("epp"));
	fBoostCheckBox = new BCheckBox("boost", B_TRANSLATE("Boost"),
		new BMessage);
	fPowerLongText = new BTextControl("powerlong",
		B_TRANSLATE("Sustained limit (W):"), "", new BMessage);
	fPowerShortText = new BTextControl("powershort",
		B_TRANSLATE("Short-term limit (W):"), "", new BMessage);

	fArmFreqMenu = new BMenuField("armfreq", B_TRANSLATE("CPU clock:"),
		new BMenu("armfreq"));
	fOverVoltageText = new BTextControl("overvoltage",
		B_TRANSLATE("Core voltage offset:"), "", new BMessage);
	fSdramFreqMenu = new BMenuField("sdramfreq",
		B_TRANSLATE("Memory clock:"), new BMenu("sdramfreq"));
	fGpuFreqMenu = new BMenuField("gpufreq", B_TRANSLATE("GPU clock:"),
		new BMenu("gpufreq"));
	fRpiNote = new BStringView("rpinote", B_TRANSLATE(
		"These values live in config.txt and apply at the next boot."));

	BBox* cpuBox = new BBox("cpubox");
	cpuBox->SetLabel(B_TRANSLATE("CPU"));
	BLayoutBuilder::Group<>(cpuBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fDriverString)
		.AddGrid()
			.Add(fGovernorMenu, 0, 0)
			.Add(fCurrentFreqString, 1, 0)
		.End()
		.AddGrid()
			.Add(fMinFreqMenu, 0, 1)
			.Add(fMaxFreqMenu, 1, 1)
			.Add(fBoostCheckBox, 0, 2)
			.Add(fEppMenu, 1, 2)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 0, 3, 3)
				.Add(fPowerLongText)
				.Add(fPowerShortText)
				.Add(fPowerMaxString)
			.End()
			.Add(fRpiNote, 0, 4, 3, 1)
			.Add(fArmFreqMenu, 0, 5)
			.Add(fOverVoltageText, 1, 5)
			.Add(fSdramFreqMenu, 0, 6)
			.Add(fGpuFreqMenu, 1, 6)
		.End()
		.Add(fCpuTempString)
		.SetInsets(B_USE_SMALL_SPACING);

	BBox* memoryBox = new BBox("memorybox");
	memoryBox->SetLabel(B_TRANSLATE("Memory"));
	BLayoutBuilder::Group<>(memoryBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fDimmStrings[0])
		.Add(fDimmStrings[1])
		.Add(fDimmStrings[2])
		.Add(fDimmStrings[3])
		.Add(fMemoryNote)
		.Add(fRamTempString)
		.SetInsets(B_USE_SMALL_SPACING);

	fGpuBox = new BBox("gpustub");
	fGpuBox->SetLabel(B_TRANSLATE("GPU"));
	BLayoutBuilder::Group<>(fGpuBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(new BStringView("gpuunimplemented", B_TRANSLATE(
			"GPU clock and power controls arrive with the GPU tab "
			"of this preflet.")))
		.SetInsets(B_USE_SMALL_SPACING);
	// Stubbed on purpose: the tab ships CPU and memory only for now.
	fGpuBox->Hide();

	BButton* applyNow = new BButton("applynow", B_TRANSLATE("Apply now"),
		new BMessage(kMsgApplyNow));
	BButton* applyBoot = new BButton("applyboot",
		B_TRANSLATE("Apply at boot"), new BMessage(kMsgApplyBoot));
	BButton* revert = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(cpuBox)
		.Add(memoryBox)
		.Add(fGpuBox)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(applyNow)
			.Add(applyBoot)
			.Add(revert)
		.End()
		.Add(fStatusString)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING);

	applyNow->SetTarget(this);
	applyBoot->SetTarget(this);
	revert->SetTarget(this);
}


OverclockView::~OverclockView()
{
	delete fBackend;
}


void
OverclockView::AttachedToWindow()
{
	BView::AttachedToWindow();

	_LoadAll();

	fTimer = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgTick), 2000000);
}


void
OverclockView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgApplyNow:
			_Apply(false);
			break;
		case kMsgApplyBoot:
			_Apply(true);
			break;
		case kMsgRevert:
			_StartHelper("revert");
			break;
		case kMsgHelperDone:
			fBusy = false;
			_SetControlsEnabled(true);
			_LoadAll();
			fStatusString->SetText(B_TRANSLATE("Changes applied."));
			break;
		case kMsgHelperOutput:
		{
			fBusy = false;
			_SetControlsEnabled(true);
			bool ok = message->GetBool("ok", false);
			if (ok && _SpdDone(*message) == B_OK) {
				fStatusString->SetText(B_TRANSLATE(
					"Memory information read."));
			} else if (!ok) {
				fStatusString->SetText(B_TRANSLATE(
					"Reading memory information failed."));
			} else
				fStatusString->SetText(B_TRANSLATE(
					"No readable memory modules."));
			break;
		}
		case kMsgTick:
			_Tick();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
OverclockView::_LoadAll()
{
	if (fBackend == NULL)
		return;

	_PopulateCpu();
	_PopulateMemory();
	_Tick();

	if (!fSpdLoaded) {
		fSpdLoaded = true;
		_StartHelper("spd", true);
	}
}


void
OverclockView::_PopulateCpu()
{
	bool bootConfig = fBackend->IsBootConfig();

	// The RPi keys and the cpufreq controls never show at once.
	if (bootConfig) {
		fGovernorMenu->Hide();
		fMinFreqMenu->Hide();
		fMaxFreqMenu->Hide();
		fBoostCheckBox->Hide();
		fArmFreqMenu->Show();
		fOverVoltageText->Show();
		fSdramFreqMenu->Show();
		fGpuFreqMenu->Show();
		fRpiNote->Show();
	} else {
		fGovernorMenu->Show();
		fMinFreqMenu->Show();
		fMaxFreqMenu->Show();
		fBoostCheckBox->Show();
		fArmFreqMenu->Hide();
		fOverVoltageText->Hide();
		fSdramFreqMenu->Hide();
		fGpuFreqMenu->Hide();
		fRpiNote->Hide();
	}
	fCurrentFreqString->Show();
	fEppMenu->Hide();
	fPowerLongText->Hide();
	fPowerShortText->Hide();
	fPowerMaxString->Hide();

	BString driver(B_TRANSLATE("Driver: "));
	driver << fBackend->Name();

	CpufreqState cpu;
	if (fBackend->GetCpu(cpu)) {
		if (!cpu.driver.empty()) {
			driver << " (" << cpu.driver.c_str() << ")";
		}
		char current[64] = "";
		if (cpu.curKHz > 0)
			_FmtMHz(current, sizeof(current), cpu.curKHz);
		fCurrentFreqString->SetText(current);

		if (!bootConfig) {
			BMenu* governorMenu = fGovernorMenu->Menu();
			while (governorMenu->CountItems() > 0)
				governorMenu->RemoveItem(0);
			for (size_t i = 0; i < cpu.governors.size(); i++) {
				fGovernorMenu->Menu()->AddItem(new BMenuItem(
					cpu.governors[i].c_str(), new BMessage));
			}
			_MarkItem(fGovernorMenu, BString(cpu.governor.c_str()));

			if (cpu.hwMinKHz > 0 && cpu.hwMaxKHz > cpu.hwMinKHz) {
				_PopulateFreqMenu(fMinFreqMenu, cpu.hwMinKHz,
					cpu.hwMaxKHz, cpu.minKHz, true);
				_PopulateFreqMenu(fMaxFreqMenu, cpu.hwMinKHz,
					cpu.hwMaxKHz, cpu.maxKHz, false);
			}

			fBoostCheckBox->SetEnabled(cpu.boostAvailable);
			fBoostCheckBox->SetValue(cpu.boostOn ? B_CONTROL_ON
				: B_CONTROL_OFF);

			if (!cpu.eppPreferences.empty()) {
				fEppMenu->Show();
				BMenu* eppMenu = fEppMenu->Menu();
				while (eppMenu->CountItems() > 0)
					eppMenu->RemoveItem(0);
				for (size_t i = 0;
						i < cpu.eppPreferences.size(); i++) {
					fEppMenu->Menu()->AddItem(new BMenuItem(
						cpu.eppPreferences[i].c_str(),
						new BMessage));
				}
				_MarkItem(fEppMenu, BString(cpu.epp.c_str()));
			}
		}
	} else {
		fStatusString->SetText(B_TRANSLATE(
			"No CPU frequency controls on this system."));
	}

	if (bootConfig) {
		RpiOverclockState rpi;
		if (fBackend->GetRpi(rpi)) {
			_PopulateFreqMenu(fArmFreqMenu, 600000, 2400000,
				rpi.hasArmFreq ? (int64)rpi.armFreq * 1000 : -1,
				false);
			_PopulateFreqMenu(fSdramFreqMenu, 400000, 1200000,
				rpi.hasSdramFreq
					? (int64)rpi.sdramFreq * 1000 : -1, false);
			_PopulateFreqMenu(fGpuFreqMenu, 250000, 1200000,
				rpi.hasGpuFreq ? (int64)rpi.gpuFreq * 1000 : -1,
				false);
			if (rpi.hasOverVoltage) {
				char offset[16];
				snprintf(offset, sizeof(offset), "%d",
					rpi.overVoltage);
				fOverVoltageText->SetText(offset);
			} else
				fOverVoltageText->SetText("");
		}
	}

	RaplState rapl;
	if (fBackend->GetRapl(rapl)) {
		fPowerLongText->Show();
		if (rapl.curShortUw >= 0)
			fPowerShortText->Show();
		else
			fPowerShortText->Hide();
		fPowerMaxString->Show();

		char watts[32];
		snprintf(watts, sizeof(watts), "%d", (int)((rapl.curLongUw
			+ 500000) / 1000000));
		fPowerLongText->SetText(watts);
		if (rapl.curShortUw >= 0) {
			snprintf(watts, sizeof(watts), "%d",
				(int)((rapl.curShortUw + 500000) / 1000000));
			fPowerShortText->SetText(watts);
		}
		char maximum[64];
		snprintf(maximum, sizeof(maximum), B_TRANSLATE("max %d W"),
			(int)((rapl.maxLongUw + 500000) / 1000000));
		fPowerMaxString->SetText(maximum);
	}

	AmdPstateState amd;
	if (fBackend->GetAmdPstate(amd)) {
		driver << " ";
		driver << B_TRANSLATE("mode:");
		driver << " ";
		driver << amd.status.c_str();
	}

	fDriverString->SetText(driver.String());
}


void
OverclockView::_PopulateFreqMenu(BMenuField* field, int64 minKHz,
	int64 maxKHz, int64 selectKHz, bool isMinimum)
{
	BMenu* menu = field->Menu();
	while (menu->CountItems() > 0)
		menu->RemoveItem(0);

	int64 step = 100000;
	if ((maxKHz - minKHz) / step > 40)
		step = 500000;

	std::vector<int64> values;
	values.push_back(minKHz);
	int64 value = ((minKHz + step) / step) * step;
	while (value < maxKHz) {
		if (value > minKHz)
			values.push_back(value);
		value += step;
	}
	values.push_back(maxKHz);
	if (selectKHz >= minKHz && selectKHz <= maxKHz
		&& std::find(values.begin(), values.end(), selectKHz)
			== values.end()) {
		values.push_back(selectKHz);
		std::sort(values.begin(), values.end());
	}

	for (size_t i = 0; i < values.size(); i++) {
		char label[32];
		_FmtMHz(label, sizeof(label), values[i]);
		BMessage* message = new BMessage;
		message->AddInt64("khz", values[i]);
		BMenuItem* item = new BMenuItem(label, message);
		menu->AddItem(item);
		if (values[i] == selectKHz)
			item->SetMarked(true);
	}

	if (menu->FindMarked() == NULL && menu->CountItems() > 0) {
		BMenuItem* fallback = isMinimum ? menu->ItemAt(0)
			: menu->ItemAt(menu->CountItems() - 1);
		fallback->SetMarked(true);
	}
}


void
OverclockView::_MarkItem(BMenuField* field, const BString& label)
{
	BMenu* menu = field->Menu();
	if (label.Length() > 0) {
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			if (label == item->Label()) {
				item->SetMarked(true);
				return;
			}
		}
	}
	if (menu->FindMarked() == NULL && menu->CountItems() > 0)
		menu->ItemAt(0)->SetMarked(true);
}


void
OverclockView::_PopulateMemory()
{
	fMemoryNote->SetText(B_TRANSLATE(
		"Timings, XMP and EXPO profiles are firmware settings; V\\OS "
		"reads them but cannot change them from here."));
}


void
OverclockView::_Tick()
{
	CpufreqState cpu;
	if (fBackend != NULL && !fBackend->IsBootConfig()
		&& fBackend->GetCpu(cpu) && cpu.curKHz > 0) {
		char current[64];
		_FmtMHz(current, sizeof(current), cpu.curKHz);
		fCurrentFreqString->SetText(current);
	}

	std::string cpuValue = OverclockFormatCelsius(fReader,
		OverclockFindSensor(fReader, OC_TEMP_CPU));
	fCpuTempString->SetText(cpuValue.c_str());
	if (!cpuValue.empty())
		fCpuTempString->Show();
	else
		fCpuTempString->Hide();

	std::string ramValue = OverclockFormatCelsius(fReader,
		OverclockFindSensor(fReader, OC_TEMP_MEMORY));
	fRamTempString->SetText(ramValue.c_str());
	if (!ramValue.empty())
		fRamTempString->Show();
	else
		fRamTempString->Hide();
}


void
OverclockView::_SetControlsEnabled(bool enabled)
{
	fGovernorMenu->SetEnabled(enabled);
	fMinFreqMenu->SetEnabled(enabled);
	fMaxFreqMenu->SetEnabled(enabled);
	fBoostCheckBox->SetEnabled(enabled);
	fEppMenu->SetEnabled(enabled);
	fPowerLongText->SetEnabled(enabled);
	fPowerShortText->SetEnabled(enabled);
	fArmFreqMenu->SetEnabled(enabled);
	fOverVoltageText->SetEnabled(enabled);
	fSdramFreqMenu->SetEnabled(enabled);
	fGpuFreqMenu->SetEnabled(enabled);
}


void
OverclockView::_StartHelper(const char* subcommand, bool spdOnly)
{
	if (fBusy)
		return;

	OverclockHelperJob* job = new OverclockHelperJob;
	job->target = BMessenger(this);
	job->spdRequest = spdOnly;
	job->arguments.Add(subcommand);

	fBusy = true;
	_SetControlsEnabled(false);

	thread_id thread = spawn_thread(_HelperThread, "overclock helper",
		B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		fBusy = false;
		_SetControlsEnabled(true);
		return;
	}

	if (!spdOnly)
		fStatusString->SetText(B_TRANSLATE(
			"Waiting for authentication..."));
}


void
OverclockView::_Apply(bool persist)
{
	if (fBackend == NULL || fBusy)
		return;

	OverclockHelperJob* job = new OverclockHelperJob;
	job->target = BMessenger(this);
	job->spdRequest = false;
	job->arguments.Add(persist ? "persist" : "apply");

	if (fBackend->IsBootConfig()) {
		BMenuItem* item = fArmFreqMenu->Menu()->FindMarked();
		if (item != NULL) {
			job->arguments.Add(BString().SetToFormat(
				"rpi_arm_freq=%d",
				(int)(_LabelToKHz(item->Label()) / 1000)));
		}
		if (fOverVoltageText->Text()[0] != '\0') {
			job->arguments.Add(BString().SetToFormat(
				"rpi_over_voltage=%d",
				atoi(fOverVoltageText->Text())));
		}
		item = fSdramFreqMenu->Menu()->FindMarked();
		if (item != NULL) {
			job->arguments.Add(BString().SetToFormat(
				"rpi_sdram_freq=%d",
				(int)(_LabelToKHz(item->Label()) / 1000)));
		}
		item = fGpuFreqMenu->Menu()->FindMarked();
		if (item != NULL) {
			job->arguments.Add(BString().SetToFormat(
				"rpi_gpu_freq=%d",
				(int)(_LabelToKHz(item->Label()) / 1000)));
		}
	} else {
		CpufreqState cpu;
		if (fBackend->GetCpu(cpu)) {
			BMenuItem* item = fGovernorMenu->Menu()->FindMarked();
			if (item != NULL) {
				job->arguments.Add(BString().SetToFormat(
					"governor=%s", item->Label()));
			}
			item = fMinFreqMenu->Menu()->FindMarked();
			if (item != NULL) {
				job->arguments.Add(BString().SetToFormat(
					"min_freq_khz=%lld",
					(long long)_LabelToKHz(item->Label())));
			}
			item = fMaxFreqMenu->Menu()->FindMarked();
			if (item != NULL) {
				job->arguments.Add(BString().SetToFormat(
					"max_freq_khz=%lld",
					(long long)_LabelToKHz(item->Label())));
			}
			if (cpu.boostAvailable) {
				job->arguments.Add(BString().SetToFormat(
					"boost=%d",
					fBoostCheckBox->Value() == B_CONTROL_ON
						? 1 : 0));
			}
			if (!cpu.eppPreferences.empty()) {
				BMenuItem* epp =
					fEppMenu->Menu()->FindMarked();
				if (epp != NULL) {
					job->arguments.Add(BString().SetToFormat(
						"epp=%s", epp->Label()));
				}
			}
		}

		RaplState rapl;
		if (fBackend->GetRapl(rapl)) {
			double watts = strtod(fPowerLongText->Text(), NULL);
			job->arguments.Add(BString().SetToFormat(
				"power_long_uw=%lld",
				(long long)(watts * 1000000.0 + 0.5)));
			if (rapl.curShortUw >= 0) {
				watts = strtod(fPowerShortText->Text(), NULL);
				job->arguments.Add(BString().SetToFormat(
					"power_short_uw=%lld",
					(long long)(watts * 1000000.0 + 0.5)));
			}
		}
	}

	fBusy = true;
	_SetControlsEnabled(false);
	fStatusString->SetText(B_TRANSLATE("Waiting for authentication..."));

	thread_id thread = spawn_thread(_HelperThread, "overclock helper",
		B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		fBusy = false;
		_SetControlsEnabled(true);
		return;
	}
}


status_t
OverclockView::_SpdDone(const BMessage& message)
{
	BString output;
	if (message.FindString("output", &output) != B_OK)
		return B_ERROR;
	int found = 0;

	for (int i = 0; i < 4; i++) {
		BString key;
		key.SetToFormat("spd_%d_hex=", i);
		int32 at = output.FindFirst(key);
		if (at < 0) {
			fDimmStrings[i]->SetText("");
			fDimmStrings[i]->Hide();
			continue;
		}
		BString hex = output.String() + at + key.Length();
		int32 end = hex.FindFirst('\n');
		if (end >= 0)
			hex.Truncate(end);

		uint8 spd[1024];
		if (_HexDecode(hex.String(), spd, sizeof(spd)) != B_OK)
			continue;

		DimmState dimm;
		dimm.slot = i;
		OverclockBackend::DecodeSpd(spd, strlen(hex.String()) / 2,
			dimm);

		BString label;
		label.SetToFormat(B_TRANSLATE("DIMM %d: "), i + 1);
		if (dimm.sizeMB > 0) {
			char size[32];
			snprintf(size, sizeof(size), "%lld MB ",
				dimm.sizeMB);
			label << size;
		}
		label << dimm.type.c_str();
		if (dimm.maxSpeedMTs > 0)
			label << "-" << dimm.maxSpeedMTs;
		if (dimm.xmp) {
			label << " ";
			label << B_TRANSLATE("XMP");
			if (dimm.xmpProfiles > 0)
				label << " (" << dimm.xmpProfiles << ")";
		}
		fDimmStrings[i]->SetText(label.String());
		fDimmStrings[i]->Show();
		found++;
	}
	return found > 0 ? B_OK : B_ERROR;
}
