/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PARTITION_PLAN_BUILDER_H
#define _PARTITION_PLAN_BUILDER_H

#include <Message.h>
#include <ObjectList.h>
#include <String.h>


namespace BPrivate {


class PartitionReference;


class PartitionOpIdMap {
public:
								~PartitionOpIdMap();

			void				Set(PartitionReference* ref,
									const BString& opID);
			const BString*		Get(PartitionReference* ref) const;

private:
			struct Entry;

			BObjectList<Entry, true> fEntries;
};


// Client side of the privilege boundary; only RunPlan() escalates via pkexec.
class PartitionPlanBuilder {
public:
								PartitionPlanBuilder();

			void				SetDisk(const char* disk);

			void				AddCreateTable(const char* id,
									const char* type);
			void				AddCreate(const char* id, off_t startMiB,
									off_t sizeMiB, const char* gptType,
									const char* role);
			void				AddDelete(const char* id,
									const char* targetRef);
			void				AddFormat(const char* id,
									const char* targetRef,
									const char* filesystem, const char* label,
									const char* role, const BMessage* options);
			void				AddSetFlags(const char* id,
									const char* targetRef, const BMessage& flags);
			void				AddResize(const char* id,
									const char* targetRef,
									const char* filesystem, off_t newSizeMiB);
			void				AddMove(const char* id,
									const char* targetRef,
									off_t newStartMiB);
			void				AddErase(const char* id,
									const char* targetRef);
			void				AddRepair(const char* id,
									const char* targetRef,
									const char* filesystem, bool checkOnly);
			void				AddSetLabel(const char* id,
									const char* targetRef,
									const char* filesystem, const char* value);
			void				AddSetGptName(const char* id,
									const char* targetRef, const char* value);

			bool				IsValid() const { return fValid; }

			BString				ToPlan() const;

	static	status_t			RunPlan(const BString& plan,
									BMessage& result, bool allowReformatEsp);

private:
			void				_AppendOptions(BString& out,
									const BMessage* options) const;
			bool				_CheckField(const char* value);

			BString				fDisk;
			BString				fTableType;
			BString				fOps;
			int32				fOpCount;
			bool				fValid;
};


}

using BPrivate::PartitionOpIdMap;
using BPrivate::PartitionPlanBuilder;

#endif
