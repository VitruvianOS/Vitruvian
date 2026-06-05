/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

//	PoseList is a commonly used instance of BObjectList<BPose>
//	Defines convenience find and iteration calls
#ifndef _POSE_LIST_H
#define _POSE_LIST_H


#include <functional>
#include <unordered_map>

#include <ObjectList.h>
#include <ObjectListPrivate.h>
#include <Node.h>

#include "Pose.h"


struct entry_ref;

namespace BPrivate {

class Model;


struct NodeRefHash {
	size_t operator()(const node_ref& r) const {
		const node_ref real = r.dereference();
		size_t h = std::hash<dev_t>()(real.device);
		h ^= std::hash<ino_t>()(real.node) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};


class PoseList : private BObjectList<BPose> {
	typedef BObjectList<BPose> _inherited;

public:
	PoseList(int32 itemsPerBlock = 20, bool owning = false)
		:
		_inherited(itemsPerBlock),
		fOwning(owning)
	{
	}

	PoseList(const PoseList& list)
		:
		_inherited(list),
		fOwning(list.fOwning)
	{
		_RebuildIndex();
	}

	~PoseList()
	{
		MakeEmpty();
	}

	::BList* AsBList() { return _inherited::Private(this).AsBList(); }
	const BObjectList<BPose>& AsObjectList() const { return *this; }

public:
	bool IsEmpty() const { return _inherited::IsEmpty(); }
	int32 CountItems() const { return _inherited::CountItems(); }
	int32 IndexOf(const BPose* p) const { return _inherited::IndexOf(p); }
	bool HasItem(const BPose* p) const { return _inherited::HasItem(p); }

	BPose* FirstItem() const { return _inherited::FirstItem(); }
	BPose* LastItem() const { return _inherited::LastItem(); }
	BPose* ItemAt(int32 i) const { return _inherited::ItemAt(i); }

	bool AddItem(BPose* p)
		{ bool r = _inherited::AddItem(p); if (r) _IndexAdd(p); return r; }
	bool AddItem(BPose* p, int32 i)
		{ bool r = _inherited::AddItem(p, i); if (r) _IndexAdd(p); return r; }
	bool AddList(PoseList* list)
		{ bool r = _inherited::AddList(list); if (r) _RebuildIndex(); return r; }

	bool RemoveItem(BPose* p, bool deleteIfOwning = true);
	BPose* RemoveItemAt(int32 i)
	{
		BPose* p = _inherited::ItemAt(i);
		if (p) _IndexRemove(p);
		return _inherited::RemoveItemAt(i);
	}

	void MakeEmpty(bool deleteIfOwning = true);

public:
	template<class Item, class Param1>
	void EachListItem(void (*func)(Item*, Param1), Param1 p1)
	{
		::EachListItem(this, func, p1);
	}

	template<class Item, class Param1, class Param2, class Param3>
	void EachListItem(void (*func)(Item*, Param1, Param2, Param3), Param1 p1, Param2 p2, Param3 p3)
	{
		::EachListItem(this, func, p1, p2, p3);
	}

public:
	BPose* FindPose(const node_ref* node, int32* index = NULL) const;
	BPose* FindPose(const entry_ref* entry, int32* index = NULL) const;
	BPose* FindPose(const Model* model, int32* index = NULL) const;
	BPose* DeepFindPose(const node_ref* node, int32* index = NULL) const;
		// same as FindPose, node can be a target of the actual
		// pose if the pose is a symlink
	PoseList* FindAllPoses(const node_ref* node) const;

	BPose* FindPoseByFileName(const char* name, int32* _index = NULL) const;

private:
	void _IndexAdd(BPose* p);
	void _IndexRemove(BPose* p);
	void _RebuildIndex();

	bool fOwning;
	std::unordered_map<node_ref, BPose*, NodeRefHash> fNodeIndex;
};


inline bool
PoseList::RemoveItem(BPose* p, bool deleteIfOwning)
{
	_IndexRemove(p);
	bool removed = _inherited::RemoveItem(p);
	if (removed && fOwning && deleteIfOwning)
		delete p;
	return removed;
}


inline void
PoseList::MakeEmpty(bool deleteIfOwning)
{
	fNodeIndex.clear();
	if (fOwning && deleteIfOwning) {
		int32 count = CountItems();
		for (int32 index = 0; index < count; index++)
			delete ItemAt(index);
	}
	_inherited::MakeEmpty();
}


// iteration glue, add permutations as needed


template<class EachParam1>
void
EachPoseAndModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, EachParam1),
	EachParam1 eachParam1)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel();
		if (model != NULL)
			(eachFunction)(pose, model, eachParam1);
	}
}


template<class EachParam1>
void
EachPoseAndModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, int32, EachParam1),
	EachParam1 eachParam1)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel();
		if (model != NULL)
			(eachFunction)(pose, model, index, eachParam1);
	}
}


template<class EachParam1, class EachParam2>
void
EachPoseAndModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, EachParam1, EachParam2),
	EachParam1 eachParam1, EachParam2 eachParam2)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel();
		if (model != NULL)
			(eachFunction)(pose, model, eachParam1, eachParam2);
	}
}


template<class EachParam1, class EachParam2>
void
EachPoseAndModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, int32, EachParam1, EachParam2),
	EachParam1 eachParam1, EachParam2 eachParam2)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel();
		if (model != NULL)
			(eachFunction)(pose, model, index, eachParam1, eachParam2);
	}
}


template<class EachParam1>
void
EachPoseAndResolvedModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, EachParam1), EachParam1 eachParam1)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel()->ResolveIfLink();
		if (model != NULL)
			(eachFunction)(pose, model, eachParam1);
	}
}


template<class EachParam1>
void
EachPoseAndResolvedModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, int32 , EachParam1),
	EachParam1 eachParam1)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel()->ResolveIfLink();
		if (model != NULL)
			(eachFunction)(pose, model, index, eachParam1);
	}
}


template<class EachParam1, class EachParam2>
void
EachPoseAndResolvedModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, EachParam1, EachParam2),
	EachParam1 eachParam1, EachParam2 eachParam2)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel()->ResolveIfLink();
		if (model != NULL)
			(eachFunction)(pose, model, eachParam1, eachParam2);
	}
}


template<class EachParam1, class EachParam2>
void
EachPoseAndResolvedModel(PoseList* list,
	void (*eachFunction)(BPose*, Model*, int32, EachParam1, EachParam2),
	EachParam1 eachParam1, EachParam2 eachParam2)
{
	for (int32 index = list->CountItems() - 1; index >= 0; index--) {
		BPose* pose = list->ItemAt(index);
		Model* model = pose->TargetModel()->ResolveIfLink();
		if (model != NULL)
			(eachFunction)(pose, model, index, eachParam1, eachParam2);
	}
}

} // namespace BPrivate

using namespace BPrivate;


#endif	// _POSE_LIST_H
