/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_PARAMETER_WEB_H
#define _MEDIA2_PARAMETER_WEB_H


#include <Flattenable.h>
#include <ObjectList.h>
#include <String.h>

#include <media2/MediaDefs.h>


class BParameterGroup;
class BParameterWeb;
class BNullParameter;
class BContinuousParameter;
class BDiscreteParameter;


// Kind constants — strings (legacy DefaultMediaTheme calls strcmp on Kind()).
extern const char* const B_GENERIC;
extern const char* const B_MASTER_GAIN;
extern const char* const B_GAIN;
extern const char* const B_BALANCE;
extern const char* const B_FREQUENCY;
extern const char* const B_LEVEL;
extern const char* const B_SHUTTLE_SPEED;
extern const char* const B_CROSSFADE;
extern const char* const B_EQUALIZATION;
extern const char* const B_COMPRESSION;
extern const char* const B_QUALITY;
extern const char* const B_BITRATE;
extern const char* const B_GOP_SIZE;
extern const char* const B_RESOLUTION;
extern const char* const B_COLOR_SPACE;
extern const char* const B_FRAME_RATE;
extern const char* const B_VIDEO_FORMAT;
extern const char* const B_WEB_PHYSICAL_INPUT;
extern const char* const B_WEB_PHYSICAL_OUTPUT;
extern const char* const B_WEB_ADC_CONVERTER;
extern const char* const B_WEB_DAC_CONVERTER;
extern const char* const B_WEB_LOGICAL_INPUT;
extern const char* const B_WEB_LOGICAL_OUTPUT;
extern const char* const B_WEB_LOGICAL_BUS;
extern const char* const B_WEB_BUFFER_INPUT;
extern const char* const B_WEB_BUFFER_OUTPUT;
extern const char* const B_SIMPLE_TRANSPORT;
extern const char* const B_INPUT_MUX;
extern const char* const B_ENABLE;
extern const char* const B_MUTE;


// Parameter flags.
enum {
	B_HIDDEN_PARAMETER = 0x01
};


class BParameter {
public:
	enum media_parameter_type {
		B_NULL_PARAMETER = 0,
		B_DISCRETE_PARAMETER,
		B_CONTINUOUS_PARAMETER,
		B_TEXT_PARAMETER
	};

	virtual							~BParameter();

			media_parameter_type	Type() const;
			int32					ID() const;
			const char*				Name() const;
			const char*				Kind() const;
			const char*				Unit() const;
			BParameterGroup*		Group() const;
			BParameterWeb*			Web() const;

			media_type				MediaType() const;
			void					SetMediaType(media_type t);

			bool					IsEnabled() const;
			void					SetEnabled(bool v);

			int32					CountChannels() const;
			void					SetChannelCount(int32 count);

			uint32					Flags() const;
			void					SetFlags(uint32 flags);

			int32					CountInputs() const;
			BParameter*				InputAt(int32 index) const;
			status_t				AddInput(BParameter* input);

			int32					CountOutputs() const;
			BParameter*				OutputAt(int32 index) const;
			status_t				AddOutput(BParameter* output);

	virtual	type_code				ValueType() = 0;

protected:
									BParameter(int32 id, media_type mediaType,
										media_parameter_type type,
										const char* name, const char* kind,
										const char* unit);

	friend class BParameterGroup;

			int32					fId;
			media_parameter_type	fType;
			media_type				fMediaType;
			BString					fName;
			BString					fKind;
			BString					fUnit;
			BParameterGroup*		fGroup;
			bool					fEnabled;
			int32					fChannelCount;
			uint32					fFlags;
			BObjectList<BParameter, false>	fInputs;
			BObjectList<BParameter, false>	fOutputs;
};


class BNullParameter : public BParameter {
public:
	virtual	type_code				ValueType() override;

protected:
									BNullParameter(int32 id, media_type mediaType,
										const char* name, const char* kind);
	friend class BParameterGroup;
};


class BContinuousParameter : public BParameter {
public:
			float					MinValue() const;
			float					MaxValue() const;
			float					ValueStep() const;

			int32					CountChannels() const;

			status_t				GetValue(void* buffer, size_t* ioSize,
										bigtime_t* lastChange);
			status_t				SetValue(void* value, size_t size,
										bigtime_t when);

	virtual	type_code				ValueType() override;

protected:
									BContinuousParameter(int32 id, media_type mediaType,
										const char* name, const char* kind,
										const char* unit, float minValue,
										float maxValue, float valueStep);
	virtual							~BContinuousParameter();
	friend class BParameterGroup;

			float					fMin;
			float					fMax;
			float					fStep;
			float*					fValues;	// per-channel
			bigtime_t				fLastChange;
};


class BDiscreteParameter : public BParameter {
public:
			int32					CountItems() const;
			const char*				ItemNameAt(int32 index) const;
			int32					ItemValueAt(int32 index) const;
			status_t				AddItem(int32 value, const char* name);

			status_t				GetValue(void* buffer, size_t* ioSize,
										bigtime_t* lastChange);
			status_t				SetValue(void* value, size_t size,
										bigtime_t when);

	virtual	type_code				ValueType() override;

protected:
									BDiscreteParameter(int32 id, media_type mediaType,
										const char* name, const char* kind);
	virtual							~BDiscreteParameter();
	friend class BParameterGroup;

			BObjectList<BString, true>	fNames;
			BObjectList<int32, true>	fValues;
			int32					fCurrent;
			bigtime_t				fLastChange;
};


class BTextParameter : public BParameter {
public:
			size_t					MaxBytes() const;

			status_t				GetValue(void* buffer, size_t* ioSize,
										bigtime_t* lastChange);
			status_t				SetValue(void* value, size_t size,
										bigtime_t when);

	virtual	type_code				ValueType() override;

protected:
									BTextParameter(int32 id, media_type mediaType,
										const char* name, const char* kind,
										size_t maxBytes);
	virtual							~BTextParameter();
	friend class BParameterGroup;

			size_t					fMaxBytes;
			char*					fText;
			bigtime_t				fLastChange;
};


class BParameterGroup {
public:
									BParameterGroup(BParameterWeb* web,
										const char* name);
									~BParameterGroup();

			const char*				Name() const;
			BParameterWeb*			Web() const;

			BNullParameter*			MakeNullParameter(int32 id, media_type mediaType,
										const char* name, const char* kind);
			BContinuousParameter*	MakeContinuousParameter(int32 id, media_type mediaType,
										const char* name, const char* kind,
										const char* unit, float minValue,
										float maxValue, float valueStep);
			BDiscreteParameter*		MakeDiscreteParameter(int32 id, media_type mediaType,
										const char* name, const char* kind);
			BTextParameter*			MakeTextParameter(int32 id, media_type mediaType,
										const char* name, const char* kind,
										size_t maxBytes);

			int32					CountParameters() const;
			BParameter*				ParameterAt(int32 index) const;

			int32					CountGroups() const;
			BParameterGroup*		GroupAt(int32 index) const;
			BParameterGroup*		MakeGroup(const char* name);

			uint32					Flags() const;
			void					SetFlags(uint32 flags);

private:
			BParameterWeb*			fWeb;
			BString					fName;
			BObjectList<BParameter, true>		fParameters;
			BObjectList<BParameterGroup, true>	fGroups;
			uint32					fFlags;
};


class BParameterWeb : public BFlattenable {
public:
									BParameterWeb();
	virtual							~BParameterWeb();

			BParameterGroup*		MakeGroup(const char* name);

			int32					CountGroups() const;
			BParameterGroup*		GroupAt(int32 index) const;

			int32					CountParameters() const;
			BParameter*				ParameterAt(int32 index) const;

			BContinuousParameter*	MakeContinuousParameter(int32 id, media_type mediaType,
										const char* name, const char* kind,
										const char* unit, float minValue,
										float maxValue, float valueStep);

	// BFlattenable
	virtual	bool					IsFixedSize() const override;
	virtual	type_code				TypeCode() const override;
	virtual	ssize_t					FlattenedSize() const override;
	virtual	status_t				Flatten(void* buffer, ssize_t size) const override;
	virtual	bool					AllowsTypeCode(type_code code) const override;
	virtual	status_t				Unflatten(type_code code, const void* buffer,
										ssize_t size) override;

private:
			BObjectList<BParameterGroup, true>	fGroups;
};


// B_MEDIA_PARAMETER_WEB_TYPE is defined in <TypeConstants.h> as 'BMCW'.
// BParameterWeb::TypeCode() reports that value.


#endif // _MEDIA2_PARAMETER_WEB_H
