/*
 * Copyright 2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEPENDENCY_EXPRESSION_H
#define _DEPENDENCY_EXPRESSION_H


#include <ObjectList.h>
#include <String.h>

#include <package/PackageDefs.h>
#include <package/PackageVersion.h>


// One atom of a dependency field; fGroup gives every member of an
// OR-group the same id, 0 meaning a plain AND term.
class VDependencyExpression {
public:
								VDependencyExpression();
								VDependencyExpression(const char* name,
									v_dependency_operator op,
									const char* version,
									int32 group = 0);

			const BString&		Name() const
									{ return fName; }
			v_dependency_operator Operator() const
									{ return fOperator; }
			const VPackageVersion& Version() const
									{ return fVersion; }
			// Also true for a lone member with a nonzero Group(); compare
			// Group() across the list to detect alternation.
			bool				IsAlternative() const
									{ return fGroup != 0; }
			int32				Group() const
									{ return fGroup; }

			void				SetTo(const char* name,
									v_dependency_operator op,
									const char* version, int32 group = 0);

			// Parses one field, e.g. "a (>= 1.0), b | c"; comma slots are
			// ANDed, "|" members share a Group() id (from 1) and are ORed.
	static	void				ParseList(const char* field,
									BObjectList<VDependencyExpression,
										true>* list);

private:
			BString				fName;
			v_dependency_operator fOperator;
			VPackageVersion		fVersion;
			int32				fGroup;
};


#endif	// _DEPENDENCY_EXPRESSION_H
