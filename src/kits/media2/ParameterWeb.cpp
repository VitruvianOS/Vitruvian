/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/ParameterWeb.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <Message.h>
#include <TypeConstants.h>


// #pragma mark - BParameter


BParameter::BParameter(int32 id, media_type mediaType, media_parameter_type type,
	const char* name, const char* kind, const char* unit)
	:
	fId(id),
	fType(type),
	fMediaType(mediaType),
	fName(name != NULL ? name : ""),
	fKind(kind != NULL ? kind : ""),
	fUnit(unit != NULL ? unit : ""),
	fGroup(NULL),
	fEnabled(true),
	fChannelCount(1),
	fFlags(0),
	fInputs(),
	fOutputs()
{
}


BParameter::~BParameter()
{
}


BParameter::media_parameter_type
BParameter::Type() const
{
	return fType;
}


int32        BParameter::ID() const         { return fId; }
const char*  BParameter::Name() const       { return fName.String(); }
const char*  BParameter::Kind() const       { return fKind.String(); }
const char*  BParameter::Unit() const       { return fUnit.String(); }
BParameterGroup* BParameter::Group() const  { return fGroup; }
BParameterWeb* BParameter::Web() const      { return fGroup != NULL ? fGroup->Web() : NULL; }
media_type   BParameter::MediaType() const  { return fMediaType; }
bool         BParameter::IsEnabled() const  { return fEnabled; }
void         BParameter::SetMediaType(media_type t) { fMediaType = t; }
void         BParameter::SetEnabled(bool v) { fEnabled = v; }

int32  BParameter::CountChannels() const   { return fChannelCount; }
void   BParameter::SetChannelCount(int32 c) { fChannelCount = c > 0 ? c : 1; }
uint32 BParameter::Flags() const           { return fFlags; }
void   BParameter::SetFlags(uint32 f)      { fFlags = f; }

int32       BParameter::CountInputs() const  { return fInputs.CountItems(); }
BParameter* BParameter::InputAt(int32 i) const  { return fInputs.ItemAt(i); }
status_t    BParameter::AddInput(BParameter* p) {
	if (p == NULL) return B_BAD_VALUE;
	fInputs.AddItem(p);
	return B_OK;
}
int32       BParameter::CountOutputs() const { return fOutputs.CountItems(); }
BParameter* BParameter::OutputAt(int32 i) const { return fOutputs.ItemAt(i); }
status_t    BParameter::AddOutput(BParameter* p) {
	if (p == NULL) return B_BAD_VALUE;
	fOutputs.AddItem(p);
	return B_OK;
}


// #pragma mark - BNullParameter


BNullParameter::BNullParameter(int32 id, media_type mediaType, const char* name,
	const char* kind)
	:
	BParameter(id, mediaType, B_NULL_PARAMETER, name, kind, NULL)
{
}


type_code
BNullParameter::ValueType()
{
	return 0;
}


// #pragma mark - BContinuousParameter


BContinuousParameter::BContinuousParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, const char* unit, float minValue,
	float maxValue, float valueStep)
	:
	BParameter(id, mediaType, B_CONTINUOUS_PARAMETER, name, kind, unit),
	fMin(minValue),
	fMax(maxValue),
	fStep(valueStep),
	fValues(NULL),
	fLastChange(0)
{
}


BContinuousParameter::~BContinuousParameter()
{
	free(fValues);
}


float BContinuousParameter::MinValue() const  { return fMin; }
float BContinuousParameter::MaxValue() const  { return fMax; }
float BContinuousParameter::ValueStep() const { return fStep; }

int32 BContinuousParameter::CountChannels() const { return BParameter::CountChannels(); }


type_code
BContinuousParameter::ValueType()
{
	return B_FLOAT_TYPE;
}


status_t
BContinuousParameter::GetValue(void* buffer, size_t* ioSize, bigtime_t* lastChange)
{
	if (ioSize == NULL)
		return B_BAD_VALUE;
	const int32 ch = CountChannels();
	const size_t need = (size_t)ch * sizeof(float);
	if (fValues == NULL) {
		fValues = (float*)calloc(ch, sizeof(float));
		if (fValues == NULL)
			return B_NO_MEMORY;
	}
	const size_t copy = *ioSize < need ? *ioSize : need;
	if (buffer != NULL && copy > 0)
		memcpy(buffer, fValues, copy);
	*ioSize = need;
	if (lastChange != NULL) *lastChange = fLastChange;
	return B_OK;
}


status_t
BContinuousParameter::SetValue(void* value, size_t size, bigtime_t when)
{
	if (value == NULL || size == 0)
		return B_BAD_VALUE;
	const int32 ch = CountChannels();
	const size_t need = (size_t)ch * sizeof(float);
	if (fValues == NULL) {
		fValues = (float*)calloc(ch, sizeof(float));
		if (fValues == NULL)
			return B_NO_MEMORY;
	}
	memcpy(fValues, value, size < need ? size : need);
	fLastChange = when;
	return B_OK;
}


// #pragma mark - BDiscreteParameter


BDiscreteParameter::BDiscreteParameter(int32 id, media_type mediaType,
	const char* name, const char* kind)
	:
	BParameter(id, mediaType, B_DISCRETE_PARAMETER, name, kind, NULL),
	fNames(),
	fValues(),
	fCurrent(0),
	fLastChange(0)
{
}


BDiscreteParameter::~BDiscreteParameter()
{
}


int32
BDiscreteParameter::CountItems() const
{
	return fNames.CountItems();
}


const char*
BDiscreteParameter::ItemNameAt(int32 index) const
{
	BString* s = fNames.ItemAt(index);
	return s != NULL ? s->String() : NULL;
}


int32
BDiscreteParameter::ItemValueAt(int32 index) const
{
	int32* v = fValues.ItemAt(index);
	return v != NULL ? *v : 0;
}


status_t
BDiscreteParameter::AddItem(int32 value, const char* name)
{
	BString* s = new(std::nothrow) BString(name != NULL ? name : "");
	int32* v   = new(std::nothrow) int32(value);
	if (s == NULL || v == NULL) {
		delete s;
		delete v;
		return B_NO_MEMORY;
	}
	fNames.AddItem(s);
	fValues.AddItem(v);
	return B_OK;
}


type_code
BDiscreteParameter::ValueType()
{
	return B_INT32_TYPE;
}


status_t
BDiscreteParameter::GetValue(void* buffer, size_t* ioSize, bigtime_t* lastChange)
{
	if (ioSize == NULL)
		return B_BAD_VALUE;
	const size_t copy = *ioSize < sizeof(int32) ? *ioSize : sizeof(int32);
	if (buffer != NULL && copy > 0)
		memcpy(buffer, &fCurrent, copy);
	*ioSize = sizeof(int32);
	if (lastChange != NULL) *lastChange = fLastChange;
	return B_OK;
}


status_t
BDiscreteParameter::SetValue(void* value, size_t size, bigtime_t when)
{
	if (value == NULL || size < sizeof(int32))
		return B_BAD_VALUE;
	fCurrent    = *(int32*)value;
	fLastChange = when;
	return B_OK;
}


// #pragma mark - BTextParameter


BTextParameter::BTextParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, size_t maxBytes)
	:
	BParameter(id, mediaType, B_TEXT_PARAMETER, name, kind, NULL),
	fMaxBytes(maxBytes),
	fText(NULL),
	fLastChange(0)
{
}


BTextParameter::~BTextParameter()
{
	free(fText);
}


size_t BTextParameter::MaxBytes() const { return fMaxBytes; }


type_code
BTextParameter::ValueType()
{
	return B_STRING_TYPE;
}


status_t
BTextParameter::GetValue(void* buffer, size_t* ioSize, bigtime_t* lastChange)
{
	if (ioSize == NULL)
		return B_BAD_VALUE;
	const size_t have = fText != NULL ? strlen(fText) + 1 : 0;
	const size_t copy = *ioSize < have ? *ioSize : have;
	if (buffer != NULL && copy > 0)
		memcpy(buffer, fText, copy);
	*ioSize = have;
	if (lastChange != NULL) *lastChange = fLastChange;
	return B_OK;
}


status_t
BTextParameter::SetValue(void* value, size_t size, bigtime_t when)
{
	if (value == NULL)
		return B_BAD_VALUE;
	const size_t use = size <= fMaxBytes ? size : fMaxBytes;
	char* nt = (char*)malloc(use + 1);
	if (nt == NULL)
		return B_NO_MEMORY;
	memcpy(nt, value, use);
	nt[use] = '\0';
	free(fText);
	fText       = nt;
	fLastChange = when;
	return B_OK;
}


// #pragma mark - BParameterGroup


BParameterGroup::BParameterGroup(BParameterWeb* web, const char* name)
	:
	fWeb(web),
	fName(name != NULL ? name : ""),
	fParameters(),
	fGroups(),
	fFlags(0)
{
}


uint32 BParameterGroup::Flags() const  { return fFlags; }
void   BParameterGroup::SetFlags(uint32 f) { fFlags = f; }


BParameterGroup::~BParameterGroup()
{
}


const char*    BParameterGroup::Name() const  { return fName.String(); }
BParameterWeb* BParameterGroup::Web() const   { return fWeb; }
int32          BParameterGroup::CountParameters() const { return fParameters.CountItems(); }
BParameter*    BParameterGroup::ParameterAt(int32 i) const { return fParameters.ItemAt(i); }
int32          BParameterGroup::CountGroups() const     { return fGroups.CountItems(); }
BParameterGroup* BParameterGroup::GroupAt(int32 i) const { return fGroups.ItemAt(i); }


BNullParameter*
BParameterGroup::MakeNullParameter(int32 id, media_type mediaType,
	const char* name, const char* kind)
{
	BNullParameter* p = new(std::nothrow) BNullParameter(id, mediaType, name, kind);
	if (p == NULL)
		return NULL;
	p->fGroup = this;
	fParameters.AddItem(p);
	return p;
}


BContinuousParameter*
BParameterGroup::MakeContinuousParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, const char* unit, float minValue,
	float maxValue, float valueStep)
{
	BContinuousParameter* p = new(std::nothrow) BContinuousParameter(id,
		mediaType, name, kind, unit, minValue, maxValue, valueStep);
	if (p == NULL)
		return NULL;
	p->fGroup = this;
	fParameters.AddItem(p);
	return p;
}


BDiscreteParameter*
BParameterGroup::MakeDiscreteParameter(int32 id, media_type mediaType,
	const char* name, const char* kind)
{
	BDiscreteParameter* p = new(std::nothrow) BDiscreteParameter(id, mediaType,
		name, kind);
	if (p == NULL)
		return NULL;
	p->fGroup = this;
	fParameters.AddItem(p);
	return p;
}


BTextParameter*
BParameterGroup::MakeTextParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, size_t maxBytes)
{
	BTextParameter* p = new(std::nothrow) BTextParameter(id, mediaType,
		name, kind, maxBytes);
	if (p == NULL)
		return NULL;
	p->fGroup = this;
	fParameters.AddItem(p);
	return p;
}


BParameterGroup*
BParameterGroup::MakeGroup(const char* name)
{
	BParameterGroup* g = new(std::nothrow) BParameterGroup(fWeb, name);
	if (g == NULL)
		return NULL;
	fGroups.AddItem(g);
	return g;
}


// #pragma mark - BParameterWeb


BParameterWeb::BParameterWeb()
	:
	fGroups()
{
}


BParameterWeb::~BParameterWeb()
{
}


BParameterGroup*
BParameterWeb::MakeGroup(const char* name)
{
	BParameterGroup* g = new(std::nothrow) BParameterGroup(this, name);
	if (g == NULL)
		return NULL;
	fGroups.AddItem(g);
	return g;
}


int32
BParameterWeb::CountGroups() const
{
	return fGroups.CountItems();
}


BParameterGroup*
BParameterWeb::GroupAt(int32 index) const
{
	return fGroups.ItemAt(index);
}


int32
BParameterWeb::CountParameters() const
{
	int32 total = 0;
	for (int32 i = 0; i < fGroups.CountItems(); i++)
		total += fGroups.ItemAt(i)->CountParameters();
	return total;
}


BParameter*
BParameterWeb::ParameterAt(int32 index) const
{
	for (int32 i = 0; i < fGroups.CountItems(); i++) {
		BParameterGroup* g = fGroups.ItemAt(i);
		const int32 n = g->CountParameters();
		if (index < n)
			return g->ParameterAt(index);
		index -= n;
	}
	return NULL;
}


BContinuousParameter*
BParameterWeb::MakeContinuousParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, const char* unit, float minValue,
	float maxValue, float valueStep)
{
	// Convenience: synthesise a single-group web for callers that don't care.
	BParameterGroup* g = fGroups.CountItems() > 0
		? fGroups.ItemAt(0)
		: MakeGroup("default");
	if (g == NULL)
		return NULL;
	return g->MakeContinuousParameter(id, mediaType, name, kind, unit,
		minValue, maxValue, valueStep);
}


// #pragma mark - BFlattenable


static void
PackParameter(BMessage& out, BParameter* p)
{
	out.AddInt32("type",       (int32)p->Type());
	out.AddInt32("id",         p->ID());
	out.AddInt32("media-type", (int32)p->MediaType());
	out.AddString("name",      p->Name());
	out.AddString("kind",      p->Kind());
	out.AddString("unit",      p->Unit());
	out.AddBool("enabled",     p->IsEnabled());
	if (p->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
		BContinuousParameter* cp = (BContinuousParameter*)p;
		out.AddFloat("min",  cp->MinValue());
		out.AddFloat("max",  cp->MaxValue());
		out.AddFloat("step", cp->ValueStep());
	} else if (p->Type() == BParameter::B_DISCRETE_PARAMETER) {
		BDiscreteParameter* dp = (BDiscreteParameter*)p;
		for (int32 i = 0; i < dp->CountItems(); i++) {
			out.AddInt32("item-value",  dp->ItemValueAt(i));
			out.AddString("item-name",  dp->ItemNameAt(i));
		}
	}
}


static void
PackGroup(BMessage& out, BParameterGroup* g)
{
	out.AddString("name", g->Name());
	for (int32 i = 0; i < g->CountParameters(); i++) {
		BMessage pm;
		PackParameter(pm, g->ParameterAt(i));
		out.AddMessage("param", &pm);
	}
	for (int32 i = 0; i < g->CountGroups(); i++) {
		BMessage gm;
		PackGroup(gm, g->GroupAt(i));
		out.AddMessage("group", &gm);
	}
}


static void
UnpackParameter(const BMessage& in, BParameterGroup* dest)
{
	int32 type = BParameter::B_NULL_PARAMETER;
	int32 id   = 0;
	int32 mt   = B_MEDIA_NO_TYPE;
	BString name, kind, unit;
	in.FindInt32("type",       &type);
	in.FindInt32("id",         &id);
	in.FindInt32("media-type", &mt);
	in.FindString("name",       &name);
	in.FindString("kind",       &kind);
	in.FindString("unit",       &unit);
	bool enabled = true;
	in.FindBool("enabled", &enabled);

	BParameter* p = NULL;
	if (type == BParameter::B_CONTINUOUS_PARAMETER) {
		float mn = 0, mx = 1, st = 0.01f;
		in.FindFloat("min", &mn);
		in.FindFloat("max", &mx);
		in.FindFloat("step", &st);
		p = dest->MakeContinuousParameter(id, (media_type)mt,
			name.String(), kind.String(), unit.String(), mn, mx, st);
	} else if (type == BParameter::B_DISCRETE_PARAMETER) {
		BDiscreteParameter* dp = dest->MakeDiscreteParameter(id,
			(media_type)mt, name.String(), kind.String());
		p = dp;
		int32 value;
		BString itemName;
		for (int32 i = 0;
				in.FindInt32("item-value", i, &value) == B_OK
				&& in.FindString("item-name", i, &itemName) == B_OK;
				i++) {
			dp->AddItem(value, itemName.String());
		}
	} else {
		p = dest->MakeNullParameter(id, (media_type)mt,
			name.String(), kind.String());
	}
	if (p != NULL)
		p->SetEnabled(enabled);
}


static void
UnpackGroup(const BMessage& in, BParameterGroup* dest)
{
	BMessage child;
	for (int32 i = 0; in.FindMessage("param", i, &child) == B_OK; i++)
		UnpackParameter(child, dest);
	for (int32 i = 0; in.FindMessage("group", i, &child) == B_OK; i++) {
		BString name;
		child.FindString("name", &name);
		BParameterGroup* sub = dest->MakeGroup(name.String());
		if (sub != NULL)
			UnpackGroup(child, sub);
	}
}


bool       BParameterWeb::IsFixedSize() const  { return false; }
type_code  BParameterWeb::TypeCode() const     { return B_MEDIA_PARAMETER_WEB_TYPE; }
	// Defined in <TypeConstants.h> as 'BMCW'.


bool
BParameterWeb::AllowsTypeCode(type_code code) const
{
	return code == B_MEDIA_PARAMETER_WEB_TYPE;
}


ssize_t
BParameterWeb::FlattenedSize() const
{
	BMessage carrier;
	for (int32 i = 0; i < fGroups.CountItems(); i++) {
		BMessage gm;
		PackGroup(gm, fGroups.ItemAt(i));
		carrier.AddMessage("group", &gm);
	}
	return carrier.FlattenedSize();
}


status_t
BParameterWeb::Flatten(void* buffer, ssize_t size) const
{
	BMessage carrier;
	for (int32 i = 0; i < fGroups.CountItems(); i++) {
		BMessage gm;
		PackGroup(gm, fGroups.ItemAt(i));
		carrier.AddMessage("group", &gm);
	}
	return carrier.Flatten((char*)buffer, size);
}


status_t
BParameterWeb::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	if (!AllowsTypeCode(code))
		return B_BAD_TYPE;
	BMessage carrier;
	status_t err = carrier.Unflatten((const char*)buffer);
	if (err != B_OK)
		return err;
	(void)size;

	// Clear existing groups before unpacking.
	while (fGroups.CountItems() > 0) {
		BParameterGroup* g = fGroups.RemoveItemAt(0);
		delete g;
	}

	BMessage gm;
	for (int32 i = 0; carrier.FindMessage("group", i, &gm) == B_OK; i++) {
		BString name;
		gm.FindString("name", &name);
		BParameterGroup* g = MakeGroup(name.String());
		if (g != NULL)
			UnpackGroup(gm, g);
	}
	return B_OK;
}
