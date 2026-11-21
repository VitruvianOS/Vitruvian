/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <SwapChain.h>

#include <new>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/dma-buf.h>

#include <AppMisc.h>
#include <PortFds.h>
#include <GbmArea.h>
#include <PortLink.h>
#include <ServerProtocol.h>
#include <View.h>
#include <ViewPrivate.h>
#include <Window.h>


namespace BPrivate {


SwapChain::SwapChain()
	:
	fView(NULL),
	fSurfaceLink(NULL),
	fToken(0),
	fViewToken(-1),
	fPresentLink(NULL),
	fWidth(0),
	fHeight(0),
	fCount(0),
	fFormat(0),
	fUsage(0),
	fNext(0),
	fImportPort(-1),
	fExternalCount(0),
	fStateArea(-1),
	fState(NULL),
	fFormatCount(0)
{
	memset(fBuffers, 0, sizeof(fBuffers));
	memset(fFormats, 0, sizeof(fFormats));
}


SwapChain::~SwapChain()
{
	Delete();
}


status_t
SwapChain::Create(BView* view, int32 width, int32 height, int32 count,
	uint32 usage, const swap_chain_format* formats, int32 formatCount)
{
	if (view == NULL || view->Window() == NULL)
		return swap_chain_enabled() ? B_BAD_VALUE : B_UNSUPPORTED;
	if (IsValid())
		return B_BUSY;

	BView::Private viewPrivate(view);
	if (!viewPrivate.LockAndSwitch())
		return B_ERROR;

	fViewToken = _get_object_token_(view);
	status_t status = _Create(viewPrivate.Link(), width, height, count, usage,
		formats, formatCount);
	if (status == B_OK)
		fView = view;
	return status;
}


status_t
SwapChain::Create(PortLink* link, int32 viewToken, int32 width, int32 height,
	int32 count, uint32 usage, const swap_chain_format* formats,
	int32 formatCount)
{
	if (link == NULL)
		return B_BAD_VALUE;
	if (IsValid())
		return B_BUSY;

	fViewToken = viewToken;
	status_t status = _Create(link, width, height, count, usage, formats,
		formatCount);
	if (status == B_OK)
		fSurfaceLink = link;
	return status;
}


status_t
SwapChain::_Create(PortLink* link, int32 width, int32 height, int32 count,
	uint32 usage, const swap_chain_format* formats, int32 formatCount)
{
	if (!swap_chain_enabled())
		return B_UNSUPPORTED;
	if (width <= 0 || height <= 0
			|| count < 2 || count > kSwapChainMaxBuffers || formatCount < 0
			|| formatCount > 4 || (formatCount > 0 && formats == NULL))
		return B_BAD_VALUE;

	link->StartMessage(AS_VIEW_CREATE_SWAP_CHAIN);
	link->Attach<int32>(width);
	link->Attach<int32>(height);
	link->Attach<int32>(count);
	link->Attach<uint32>(usage);
	link->Attach<int32>(formatCount);
	for (int32 i = 0; i < formatCount; i++) {
		link->Attach<uint32>(formats[i].fourcc);
		link->Attach<uint64>(formats[i].modifier);
	}

	status_t status = B_ERROR;
	if (link->FlushWithReply(status) != B_OK)
		return B_ERROR;
	if (status != B_OK)
		return status;

	fWidth = width;
	fHeight = height;
	fUsage = usage;
	fFormatCount = formatCount;
	for (int32 i = 0; i < formatCount; i++)
		fFormats[i] = formats[i];

	status = _ReadReply(*link);
	if (status != B_OK)
		_Abandon(*link);
	return status;
}


status_t
SwapChain::Resize(int32 width, int32 height)
{
	if (!IsValid())
		return B_NO_INIT;
	if (width <= 0 || height <= 0)
		return B_BAD_VALUE;

	if (fSurfaceLink != NULL)
		return _Resize(fSurfaceLink, width, height);

	BView::Private viewPrivate(fView);
	if (!viewPrivate.LockAndSwitch())
		return B_ERROR;
	return _Resize(viewPrivate.Link(), width, height);
}


status_t
SwapChain::_Resize(PortLink* link, int32 width, int32 height)
{
	link->StartMessage(AS_VIEW_RESIZE_SWAP_CHAIN);
	link->Attach<int32>(fToken);
	link->Attach<int32>(width);
	link->Attach<int32>(height);

	status_t status = B_ERROR;
	if (link->FlushWithReply(status) != B_OK)
		return B_ERROR;
	if (status != B_OK)
		return status;

	fWidth = width;
	fHeight = height;
	status = _ReadReply(*link);
	if (status != B_OK)
		_Abandon(*link);
	return status;
}


void
SwapChain::Delete()
{
	if (!IsValid())
		return;

	if (fSurfaceLink != NULL) {
		fSurfaceLink->StartMessage(AS_VIEW_DELETE_SWAP_CHAIN);
		fSurfaceLink->Attach<int32>(fToken);
		fSurfaceLink->Flush();
		_Unmap();
		fToken = 0;
		fSurfaceLink = NULL;
		return;
	}

	// Without the window the server drops the chain with the view.
	BWindow* window = fView != NULL ? fView->Window() : NULL;
	if (window != NULL && window->IsLocked()) {
		BView::Private viewPrivate(fView);
		if (viewPrivate.LockAndSwitch()) {
			PortLink* link = viewPrivate.Link();
			link->StartMessage(AS_VIEW_DELETE_SWAP_CHAIN);
			link->Attach<int32>(fToken);
			link->Flush();
		}
	}

	_Unmap();
	fToken = 0;
	fView = NULL;
	fSurfaceLink = NULL;
}


status_t
SwapChain::BufferAt(int32 index, swap_chain_buffer* buffer) const
{
	if (!IsValid() || index < 0 || index >= fCount || buffer == NULL)
		return B_BAD_VALUE;

	*buffer = fBuffers[index];
	return B_OK;
}


int
SwapChain::BufferFd(int32 index) const
{
	if (!IsValid() || index < 0 || index >= fCount)
		return -1;

	return _kern_area_fd(fBuffers[index].area);
}


int32
SwapChain::Acquire(bigtime_t timeout)
{
	if (!IsValid())
		return B_NO_INIT;

	const bigtime_t deadline = timeout == B_INFINITE_TIMEOUT
		? B_INFINITE_TIMEOUT : system_time() + timeout;

	while (true) {
		for (int32 i = 0; i < fCount; i++) {
			const int32 index = (fNext + i) % fCount;
			int32 expected = kSwapBufferFree;
			if (__atomic_compare_exchange_n(&fState->buffer[index], &expected,
					(int32)kSwapBufferClient, false, __ATOMIC_ACQUIRE,
					__ATOMIC_RELAXED)) {
				fNext = (index + 1) % fCount;
				return index;
			}
		}

		if (timeout == 0 || (deadline != B_INFINITE_TIMEOUT
				&& system_time() >= deadline))
			return B_TIMED_OUT;

		snooze(500);
	}
}


status_t
SwapChain::Present(int32 index, const BRect* damage, int32 damageCount,
	uint32 flags)
{
	if (!IsValid() || index < 0 || index >= fCount || damageCount < 0
			|| (damageCount > 0 && damage == NULL))
		return B_BAD_VALUE;

	int32 expected = kSwapBufferClient;
	if (!__atomic_compare_exchange_n(&fState->buffer[index], &expected,
			(int32)kSwapBufferShown, false, __ATOMIC_RELEASE,
			__ATOMIC_RELAXED))
		return B_BAD_VALUE;

	if (fPresentLink == NULL)
		return B_NO_INIT;

	// Past the cap the damage is dropped and the whole buffer shown.
	if (damageCount > kSwapChainMaxDamage)
		damageCount = 0;

	PortLink* link = fPresentLink;
	link->StartMessage(AS_VIEW_PRESENT);
	link->Attach<int32>(fViewToken);
	link->Attach<int32>(fToken);
	link->Attach<int32>(index);
	link->Attach<uint32>(flags);
	link->Attach<int32>(damageCount);
	for (int32 i = 0; i < damageCount; i++)
		link->Attach<BRect>(damage[i]);

	// A full window port must not stall the renderer.
	status_t status = link->Flush(100000);
	if (status != B_OK)
		__atomic_store_n(&fState->buffer[index], (int32)kSwapBufferFree,
			__ATOMIC_RELEASE);
	return status;
}


status_t
SwapChain::PresentWithFence(int32 index, int fenceFd, const BRect* damage,
	int32 damageCount, uint32 flags)
{
	if (!IsValid() || index < 0 || index >= fCount)
		return B_BAD_VALUE;

#ifdef DMA_BUF_IOCTL_IMPORT_SYNC_FILE
	if (fenceFd >= 0) {
		const int fd = BufferFd(index);
		if (fd >= 0) {
			struct dma_buf_import_sync_file import;
			import.flags = DMA_BUF_SYNC_WRITE;
			import.fd = fenceFd;
			if (ioctl(fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &import) == 0)
				flags |= kPresentHasFence;
			close(fd);
		}
	}
#endif
	return Present(index, damage, damageCount, flags);
}


namespace {

const int32 kImportCode = 'xImp';

struct import_message {
	// -1 forgets every buffer.
	int32					slot;
	swap_chain_external		buffer;
};

}


status_t
SwapChain::ExternalImport(int fd, const swap_chain_external& buffer,
	int32* slot)
{
	if (!IsValid() || fImportPort < 0 || fd < 0 || slot == NULL)
		return B_NO_INIT;
	if (fExternalCount >= kSwapChainMaxExternal)
		return B_NO_MEMORY;

	import_message message;
	message.slot = fExternalCount;
	message.buffer = buffer;
	status_t status = write_port_fds(fImportPort, kImportCode, &message,
		sizeof(message), &fd, 1, B_RELATIVE_TIMEOUT, 100000);
	if (status != B_OK)
		return status;

	__atomic_store_n(&fState->external[fExternalCount],
		(int32)kSwapBufferFree, __ATOMIC_RELEASE);
	*slot = fExternalCount++;
	return B_OK;
}


void
SwapChain::ExternalReset()
{
	if (!IsValid() || fImportPort < 0 || fExternalCount == 0)
		return;

	import_message message;
	memset(&message, 0, sizeof(message));
	message.slot = -1;
	write_port_fds(fImportPort, kImportCode, &message, sizeof(message), NULL,
		0, B_RELATIVE_TIMEOUT, 100000);
	fExternalCount = 0;
}


status_t
SwapChain::PresentExternal(int32 slot, const BRect* damage, int32 damageCount)
{
	if (!IsValid() || slot < 0 || slot >= fExternalCount || damageCount < 0
			|| (damageCount > 0 && damage == NULL))
		return B_BAD_VALUE;
	if (fPresentLink == NULL)
		return B_NO_INIT;

	int32 expected = kSwapBufferFree;
	if (!__atomic_compare_exchange_n(&fState->external[slot], &expected,
			(int32)kSwapBufferShown, false, __ATOMIC_RELEASE,
			__ATOMIC_RELAXED))
		return B_BUSY;

	if (damageCount > kSwapChainMaxDamage)
		damageCount = 0;

	PortLink* link = fPresentLink;
	link->StartMessage(AS_VIEW_PRESENT);
	link->Attach<int32>(fViewToken);
	link->Attach<int32>(-fToken);
	link->Attach<int32>(slot);
	link->Attach<uint32>(kPresentExternal);
	link->Attach<int32>(damageCount);
	for (int32 i = 0; i < damageCount; i++)
		link->Attach<BRect>(damage[i]);

	status_t status = link->Flush(100000);
	if (status != B_OK)
		__atomic_store_n(&fState->external[slot], (int32)kSwapBufferFree,
			__ATOMIC_RELEASE);
	return status;
}


bool
SwapChain::ExternalReleased(int32 slot) const
{
	return fState != NULL && slot >= 0 && slot < kSwapChainMaxExternal
		&& __atomic_load_n(&fState->external[slot], __ATOMIC_ACQUIRE)
			== kSwapBufferFree;
}


bool
SwapChain::ExternalRefused() const
{
	return fState != NULL
		&& __atomic_load_n(&fState->externalRefused, __ATOMIC_ACQUIRE) != 0;
}


bigtime_t
SwapChain::LastReleaseTime() const
{
	return fState != NULL ? fState->releaseTime : 0;
}


int32
SwapChain::RefreshPeriod() const
{
	return fState != NULL ? fState->refreshPeriod : 0;
}


status_t
SwapChain::_ReadReply(PortLink& link)
{
	struct {
		area_id		area;
		uint32		fourcc;
		uint64		modifier;
		uint32		stride;
		uint32		offset;
	} buffers[kSwapChainMaxBuffers];
	int32 token = 0;
	int32 count = 0;
	uint32 format = 0;
	area_id stateArea = -1;
	port_id windowPort = -1;
	port_id importPort = -1;

	link.Read<int32>(&token);
	link.Read<int32>(&count);
	link.Read<uint32>(&format);
	if (link.Read<area_id>(&stateArea) != B_OK || token == 0 || count < 2
			|| count > kSwapChainMaxBuffers)
		return B_ERROR;

	for (int32 i = 0; i < count; i++) {
		link.Read<area_id>(&buffers[i].area);
		link.Read<uint32>(&buffers[i].fourcc);
		link.Read<uint64>(&buffers[i].modifier);
		link.Read<uint32>(&buffers[i].stride);
		if (link.Read<uint32>(&buffers[i].offset) != B_OK)
			return B_ERROR;
	}
	link.Read<port_id>(&importPort);
	if (link.Read<port_id>(&windowPort) != B_OK || windowPort < 0)
		return B_ERROR;

	_Unmap();
	fToken = token;
	fImportPort = importPort;
	fExternalCount = 0;
	if (fPresentLink == NULL)
		fPresentLink = new(std::nothrow) PortLink(windowPort);
	if (fPresentLink == NULL)
		return B_NO_MEMORY;

	void* address = NULL;
	fStateArea = clone_area("swap chain state", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, stateArea);
	if (fStateArea < 0) {
		status_t status = fStateArea;
		fStateArea = -1;
		return status;
	}
	fState = (swap_chain_state*)address;

	for (int32 i = 0; i < count; i++) {
		address = NULL;
		area_id clone = clone_area("swap chain buffer", &address,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, buffers[i].area);
		if (clone < 0) {
			fCount = i;
			_Unmap();
			return clone;
		}

		fBuffers[i].area = clone;
		fBuffers[i].fourcc = buffers[i].fourcc;
		fBuffers[i].modifier = buffers[i].modifier;
		fBuffers[i].stride = buffers[i].stride;
		fBuffers[i].offset = buffers[i].offset;
		fBuffers[i].bits = address;
	}

	fCount = count;
	fFormat = format;
	fNext = 0;
	return B_OK;
}


void
SwapChain::_Unmap()
{
	for (int32 i = 0; i < fCount; i++) {
		if (fBuffers[i].area >= 0)
			delete_area(fBuffers[i].area);
	}
	memset(fBuffers, 0, sizeof(fBuffers));
	if (fStateArea >= 0)
		delete_area(fStateArea);

	fStateArea = -1;
	fState = NULL;
	fCount = 0;
	fImportPort = -1;
	fExternalCount = 0;
	delete fPresentLink;
	fPresentLink = NULL;
}


// The server holds a chain we cannot map.
void
SwapChain::_Abandon(PortLink& link)
{
	link.StartMessage(AS_VIEW_DELETE_SWAP_CHAIN);
	link.Attach<int32>(fToken);
	link.Flush();
	_Unmap();
	fToken = 0;
	fView = NULL;
}


}	// namespace BPrivate
