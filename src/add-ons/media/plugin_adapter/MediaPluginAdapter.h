/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef __MEDIA_PLUGIN_ADAPTER_H__
#define __MEDIA_PLUGIN_ADAPTER_H__

#include <atomic>
#include <vector>

#include <String.h>

#include <media2/Controllable.h>
#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>

#include "LadspaAbi.h"


// In-process LV2/LADSPA host as a BMediaUnit (§2.8, §2.10 leaning).
// BMediaAddOn is discovery only; hosting lives here.
class BMediaPluginAdapter : public BMediaUnit {
public:
							BMediaPluginAdapter(const char* name = "Plugin");
	virtual					~BMediaPluginAdapter();

			status_t		InitCheck() const;

			status_t		LoadLadspa(const char* path, unsigned long index);
#ifdef VOS_HAS_LILV
			status_t		LoadLv2(const char* uri);
#endif

			const char*		PluginName() const;
			int32			AudioInputCount() const;
			int32			AudioOutputCount() const;
			int32			ParameterCount() const;

			// Property ids for control ports.
	enum {
		kPropPortBase = B_MEDIA_PROP_CUSTOM_BASE + 1
	};

protected:
	virtual	void			ProcessCycle(uint32 frameCount) override;
	virtual	status_t		Start() override;

private:
			void			_PublishAndBind();
			void			_ApplyParam(int32 id, const void* value,
								size_t size);
	static	void			_ChangeHook(int32 id, bigtime_t when,
								const void* value, size_t size, void* cookie);
			void			_SetupPorts();
			void			_TeardownPlugin();

			BString			fPluginName;
			void*			fModule;
			const LADSPA_Descriptor*	fDescriptor;
			LADSPA_Handle	fHandle;
			std::vector<BMediaInput*>	fInputs;
			std::vector<BMediaOutput*>	fOutputs;
			std::vector<float*>		fPortBuffers;
			std::vector<uint32>		fPortIsInput;
			std::vector<float>		fControlValues;
			BControllable*	fControllable;
			std::atomic<uint32>	fConfiguredRate;
			std::atomic<uint32>	fBlockSize;
			status_t		fInitStatus;

#ifdef VOS_HAS_LILV
			// LV2 state lives in a pimpl once lilv is available.
			void*			fLv2;
#endif
};


#endif //__MEDIA_PLUGIN_ADAPTER_H__
