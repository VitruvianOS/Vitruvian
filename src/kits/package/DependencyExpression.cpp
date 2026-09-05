/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <package/DependencyExpression.h>

#include <ctype.h>


VDependencyExpression::VDependencyExpression()
	:
	fOperator(V_DEPENDENCY_OP_NONE),
	fGroup(0)
{
}


VDependencyExpression::VDependencyExpression(const char* name,
	v_dependency_operator op, const char* version, int32 group)
	:
	fName(name),
	fOperator(op),
	fVersion(version),
	fGroup(group)
{
}


void
VDependencyExpression::SetTo(const char* name, v_dependency_operator op,
	const char* version, int32 group)
{
	fName = name;
	fOperator = op;
	fVersion.SetTo(version);
	fGroup = group;
}


// "<" and ">" are legacy loose forms of "<=" and ">="; anything
// unrecognised keeps the whole parenthesised text as the version.
static void
parse_qualifier(const BString& term, BString* name, v_dependency_operator* op,
	BString* version)
{
	int32 paren = term.FindFirst('(');
	if (paren < 0) {
		BString trimmed(term);
		trimmed.Trim();
		*name = trimmed;
		*op = V_DEPENDENCY_OP_NONE;
		version->SetTo("");
		return;
	}

	BString head;
	term.CopyInto(head, 0, paren);
	head.Trim();
	*name = head;

	int32 close = term.FindFirst(')', paren);
	BString inside;
	term.CopyInto(inside, paren + 1,
		(close >= 0 ? close : term.Length()) - paren - 1);
	inside.Trim();

	int32 pos = 0;
	while (pos < inside.Length()
			&& (inside[pos] == '<' || inside[pos] == '>'
				|| inside[pos] == '=')) {
		pos++;
	}
	BString opText;
	inside.CopyInto(opText, 0, pos);
	BString rest;
	inside.CopyInto(rest, pos, inside.Length() - pos);
	rest.Trim();
	*version = rest;

	if (opText == "<<")
		*op = V_DEPENDENCY_OP_LT;
	else if (opText == "<=" || opText == "<")
		*op = V_DEPENDENCY_OP_LE;
	else if (opText == "=")
		*op = V_DEPENDENCY_OP_EQ;
	else if (opText == ">=" || opText == ">")
		*op = V_DEPENDENCY_OP_GE;
	else if (opText == ">>")
		*op = V_DEPENDENCY_OP_GT;
	else {
		*op = V_DEPENDENCY_OP_NONE;
		*version = inside;
	}
}


void
VDependencyExpression::ParseList(const char* field,
	BObjectList<VDependencyExpression, true>* list)
{
	if (field == NULL || list == NULL)
		return;

	BString text(field);
	int32 nextGroup = 1;

	int32 start = 0;
	while (start <= text.Length()) {
		int32 comma = text.FindFirst(',', start);
		BString slot;
		text.CopyInto(slot, start,
			(comma >= 0 ? comma : text.Length()) - start);
		start = (comma >= 0) ? comma + 1 : text.Length() + 1;

		slot.Trim();
		if (slot.Length() == 0)
			continue;

		// A "|" slot is one OR-group; single terms are plain AND, group 0.
		BObjectList<BString, true> alternatives(4);
		int32 altStart = 0;
		while (altStart <= slot.Length()) {
			int32 bar = slot.FindFirst('|', altStart);
			BString alt;
			slot.CopyInto(alt, altStart,
				(bar >= 0 ? bar : slot.Length()) - altStart);
			altStart = (bar >= 0) ? bar + 1 : slot.Length() + 1;
			alternatives.AddItem(new BString(alt));
		}

		const int32 group = (alternatives.CountItems() > 1)
			? nextGroup++ : 0;

		for (int32 i = 0; i < alternatives.CountItems(); i++) {
			BString name, version;
			v_dependency_operator op;
			parse_qualifier(*alternatives.ItemAt(i), &name, &op, &version);
			if (name.Length() == 0)
				continue;
			list->AddItem(new VDependencyExpression(name.String(), op,
				version.String(), group));
		}
	}
}
