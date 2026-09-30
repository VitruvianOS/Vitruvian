/*
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2026, Dario Casalinuovo.
 * This file may be used under the terms of the MIT License.
 */
#ifndef SCREEN_CONFIGURATIONS_H
#define SCREEN_CONFIGURATIONS_H


#include <Accelerant.h>
#include <Rect.h>

#include <ObjectList.h>


class BMessage;


struct screen_configuration {
	int32			id;
	monitor_info	info;
	BRect			frame;
	display_mode	mode;
	float			brightness;
	int32			rotation;
	int32			reflection;
	float			temperature;
	bool			has_info;
	bool			is_current;
};


class ScreenConfigurations {
public:
								ScreenConfigurations();
								~ScreenConfigurations();

			screen_configuration* CurrentByID(int32 id) const;
			screen_configuration* BestFit(int32 id, const monitor_info* info,
									bool* _exactMatch = NULL) const;

			status_t			Set(int32 id, const monitor_info* info,
									const BRect& frame,
									const display_mode& mode);
			void				SetBrightness(int32 id, float brightness);
			float				Brightness(int32 id);
			void				SetRotation(int32 id, int32 rotation);
			int32				Rotation(int32 id);
			void				SetReflection(int32 id, int32 reflection);
			int32				Reflection(int32 id);
			void				SetTemperature(int32 id, float temperature);
			float				Temperature(int32 id);
			void				Remove(screen_configuration* configuration);

			status_t			Store(BMessage& settings) const;
			status_t			Restore(const BMessage& settings);

private:
	typedef BObjectList<screen_configuration, true> ConfigurationList;

			screen_configuration* _FindByID(int32 id) const;

			ConfigurationList	fConfigurations;
};


#endif	// SCREEN_CONFIGURATIONS_H

