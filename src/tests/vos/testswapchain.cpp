/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * testswapchain: a moving colour bar through a view swap chain.
 *
 *   VOS_SWAP_CHAIN=1 testswapchain
 */


#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <MessageRunner.h>
#include <View.h>
#include <Window.h>

#include <SwapChain.h>


using namespace BPrivate;


static const uint32 kTick = 'tick';


class ChainView : public BView {
public:
	ChainView(BRect frame)
		:
		BView(frame, "chain", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FRAME_EVENTS),
		fRunner(NULL),
		fFrame(0)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual ~ChainView()
	{
		delete fRunner;
	}

	virtual void AttachedToWindow()
	{
		BRect bounds = Bounds();
		const swap_chain_format format = { kSwapFormatXRGB8888,
			kSwapModifierLinear };
		status_t status = fChain.Create(this, bounds.IntegerWidth() + 1,
			bounds.IntegerHeight() + 1, 3, kSwapUsageRender, &format, 1);
		if (status != B_OK) {
			fprintf(stderr, "swap chain: %s\n", strerror(status));
			return;
		}

		BMessage tick(kTick);
		fRunner = new BMessageRunner(BMessenger(this), &tick, 16000);
	}

	virtual void DetachedFromWindow()
	{
		delete fRunner;
		fRunner = NULL;
		fChain.Delete();
	}

	virtual void FrameResized(float width, float height)
	{
		if (fChain.IsValid()) {
			status_t status = fChain.Resize((int32)width + 1,
				(int32)height + 1);
			if (status != B_OK)
				fprintf(stderr, "resize: %s\n", strerror(status));
		}
	}

	virtual void Draw(BRect)
	{
		// the server shows the last presented buffer
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kTick)
			_Render();
		else
			BView::MessageReceived(message);
	}

private:
	void _Render()
	{
		if (!fChain.IsValid())
			return;

		int32 index = fChain.Acquire(0);
		if (index < 0)
			return;

		swap_chain_buffer buffer;
		if (fChain.BufferAt(index, &buffer) != B_OK)
			return;

		const int32 width = fChain.Width();
		const int32 height = fChain.Height();
		const int32 bar = (fFrame * 6) % width;
		for (int32 y = 0; y < height; y++) {
			uint32* row = (uint32*)((uint8*)buffer.bits + buffer.offset
				+ (size_t)y * buffer.stride);
			const uint32 background = 0x00202040 + ((y * 255 / height) << 8);
			for (int32 x = 0; x < width; x++)
				row[x] = (x >= bar && x < bar + 40) ? 0x00ffcc00 : background;
		}
		fFrame++;

		BRect damage(0, 0, width - 1, height - 1);
		fChain.Present(index, &damage, 1);
	}

	SwapChain		fChain;
	BMessageRunner*	fRunner;
	int32			fFrame;
};


class ChainWindow : public BWindow {
public:
	ChainWindow()
		:
		BWindow(BRect(100, 100, 499, 399), "Swap chain", B_TITLED_WINDOW,
			B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new ChainView(Bounds()));
	}
};


int
main()
{
	BApplication app("application/x-vnd.vos-testswapchain");
	(new ChainWindow())->Show();
	app.Run();
	return 0;
}
