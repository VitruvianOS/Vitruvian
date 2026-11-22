/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "DmabufPresenter.h"
#include "logging.h"

#include <View.h>

#include <new>

using namespace BPrivate;


DmabufPresenter::DmabufPresenter()
	:
	fChain(nullptr),
	fSlotCount(0)
{
	for (int i = 0; i < kSwapChainMaxExternal; i++)
		fSlots[i] = nullptr;
}


DmabufPresenter::~DmabufPresenter()
{
	Detach();
}


bool
DmabufPresenter::Attach(BView* view, int width, int height)
{
	if (!swap_chain_enabled() || width <= 0 || height <= 0)
		return false;

	std::lock_guard<std::mutex> lock(fMutex);
	if (fChain != nullptr)
		return true;

	SwapChain* chain = new(std::nothrow) SwapChain;
	if (chain == nullptr)
		return false;

	const swap_chain_format format = { kSwapFormatXRGB8888,
		kSwapModifierLinear };
	status_t status = chain->Create(view, width, height, 2, kSwapUsageRender,
		&format, 1);
	if (status != B_OK) {
		log_info("DmabufPresenter: no swap chain (%s)", strerror(status));
		delete chain;
		return false;
	}

	fChain = chain;
	return true;
}


void
DmabufPresenter::Resize(int width, int height)
{
	std::lock_guard<std::mutex> lock(fMutex);
	if (fChain == nullptr || width <= 0 || height <= 0
			|| (fChain->Width() == width && fChain->Height() == height))
		return;

	// A new state area replaces the old one: what was imported is gone.
	_ForgetSlotsLocked();
	if (fChain->Resize(width, height) != B_OK) {
		log_error("DmabufPresenter: resize failed, back to copies");
		fChain->Delete();
		delete fChain;
		fChain = nullptr;
	}
}


void
DmabufPresenter::Detach()
{
	std::vector<WPEBuffer*> dead;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		_ForgetSlotsLocked();
		for (size_t i = 0; i < fEntries.size(); i++)
			dead.push_back(fEntries[i].buffer);
		fEntries.clear();
		if (fChain != nullptr) {
			fChain->Delete();
			delete fChain;
			fChain = nullptr;
		}
	}
	// Plain refcount drops; no callbacks run from here.
	for (size_t i = 0; i < dead.size(); i++)
		g_object_unref(dead[i]);
}


bool
DmabufPresenter::IsReady() const
{
	std::lock_guard<std::mutex> lock(fMutex);
	return fChain != nullptr;
}


// fMutex held. Outstanding entries keep their own reference.
void
DmabufPresenter::_ForgetSlotsLocked()
{
	if (fChain != nullptr && fSlotCount > 0)
		fChain->ExternalReset();
	for (int i = 0; i < fSlotCount; i++) {
		if (fSlots[i] != nullptr)
			g_object_unref(fSlots[i]);
		fSlots[i] = nullptr;
	}
	fSlotCount = 0;
	for (size_t i = 0; i < fEntries.size(); i++)
		fEntries[i].slot = -1;
}


DmabufPresenter::Result
DmabufPresenter::Present(WPEBuffer* buffer, int fd,
	const BPrivate::swap_chain_external& description, const WPERectangle* damage,
	unsigned int damageCount)
{
	std::vector<WPEBuffer*> rendered;
	std::vector<WPEBuffer*> released;
	Result result = kUnsupported;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		if (fChain == nullptr || fChain->ExternalRefused()
				|| (int32)description.width != fChain->Width()
				|| (int32)description.height != fChain->Height())
			return kUnsupported;

		int slot = -1;
		for (int i = 0; i < fSlotCount; i++) {
			if (fSlots[i] == buffer)
				slot = i;
		}
		if (slot < 0) {
			// The server keeps four; start a new set when they are used.
			if (fSlotCount == kSwapChainMaxExternal)
				_ForgetSlotsLocked();

			int32 imported = -1;
			if (fChain->ExternalImport(fd, description, &imported) != B_OK)
				return kUnsupported;
			slot = imported;
			fSlots[slot] = (WPEBuffer*)g_object_ref(buffer);
			fSlotCount = slot + 1;
		}

		BRect rects[kSwapChainMaxDamage];
		int32 count = 0;
		if (damage != nullptr && damageCount > 0
				&& damageCount <= (unsigned)kSwapChainMaxDamage) {
			for (unsigned i = 0; i < damageCount; i++) {
				rects[count++].Set(damage[i].x, damage[i].y,
					damage[i].x + damage[i].width - 1,
					damage[i].y + damage[i].height - 1);
			}
		} else {
			rects[count++].Set(0, 0, description.width - 1,
				description.height - 1);
		}

		status_t status = fChain->PresentExternal(slot, rects, count);
		if (status == B_BUSY)
			return kBusy;
		if (status != B_OK)
			return kUnsupported;

		Entry entry = { (WPEBuffer*)g_object_ref(buffer), slot, false };
		fEntries.push_back(entry);
		result = kShown;
		_Collect(rendered, released);
	}

	if (fRendered) {
		for (size_t i = 0; i < rendered.size(); i++)
			fRendered(rendered[i]);
	}
	if (fReleased) {
		for (size_t i = 0; i < released.size(); i++)
			fReleased(released[i]);
	}
	for (size_t i = 0; i < released.size(); i++)
		g_object_unref(released[i]);
	for (size_t i = 0; i < rendered.size(); i++)
		g_object_unref(rendered[i]);
	return result;
}


// fMutex held. A frame is done once app_server shows it, which is when it
// releases the one before; the oldest has nothing before it. A buffer is
// free once the server shows something else. Every returned buffer carries
// one reference for the caller to drop.
void
DmabufPresenter::_Collect(std::vector<WPEBuffer*>& rendered,
	std::vector<WPEBuffer*>& released)
{
	const size_t count = fEntries.size();
	std::vector<char> gone(count, 0);
	for (size_t i = 0; i < count; i++) {
		const Entry& entry = fEntries[i];
		// A forgotten slot is no longer imported by the server.
		gone[i] = entry.slot < 0 || fChain == nullptr
			|| fChain->ExternalReleased(entry.slot);
	}

	std::vector<Entry> keep;
	for (size_t i = 0; i < count; i++) {
		Entry entry = fEntries[i];
		const bool shown = i == 0 || gone[i - 1];
		if (!entry.rendered && (shown || gone[i])) {
			entry.rendered = true;
			rendered.push_back((WPEBuffer*)g_object_ref(entry.buffer));
		}
		if (gone[i])
			released.push_back(entry.buffer);
		else
			keep.push_back(entry);
	}
	fEntries.swap(keep);
}


bool
DmabufPresenter::Poll()
{
	std::vector<WPEBuffer*> rendered;
	std::vector<WPEBuffer*> released;
	bool outstanding;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		_Collect(rendered, released);
		outstanding = !fEntries.empty();
	}

	if (fRendered) {
		for (size_t i = 0; i < rendered.size(); i++)
			fRendered(rendered[i]);
	}
	if (fReleased) {
		for (size_t i = 0; i < released.size(); i++)
			fReleased(released[i]);
	}
	for (size_t i = 0; i < released.size(); i++)
		g_object_unref(released[i]);
	for (size_t i = 0; i < rendered.size(); i++)
		g_object_unref(rendered[i]);
	return outstanding;
}


void
DmabufPresenter::Flush()
{
	std::vector<Entry> entries;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		entries.swap(fEntries);
		_ForgetSlotsLocked();
	}

	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].rendered && fRendered)
			fRendered(entries[i].buffer);
		if (fReleased)
			fReleased(entries[i].buffer);
		g_object_unref(entries[i].buffer);
	}
}


void
DmabufPresenter::SetCallbacks(std::function<void(WPEBuffer*)> rendered,
	std::function<void(WPEBuffer*)> released)
{
	fRendered = rendered;
	fReleased = released;
}
