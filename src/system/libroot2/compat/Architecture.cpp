/*
 * Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <architecture_private.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <OS.h>

#include <directories.h>
#include <find_directory_private.h>


static const char* const kArchitecture = "x86_64";
static const char* const kPrimaryArchitecture = "x86_64";

static const char* const kSiblingArchitectures[] = {};

static const size_t kSiblingArchitectureCount
	= sizeof(kSiblingArchitectures) / sizeof(const char*);


static bool
has_secondary_architecture(const char* architecture)
{
	return false;
}


// #pragma mark -


const char*
__get_architecture()
{
	return kArchitecture;
}


const char*
__get_primary_architecture()
{
	return kPrimaryArchitecture;
}


size_t
__get_secondary_architectures(const char** architectures, size_t count)
{
	return 0;
}


size_t
__get_architectures(const char** architectures, size_t count)
{
	architectures[0] = __get_primary_architecture();
	return 1;
}


const char*
__guess_architecture_for_path(const char* path)
{
	return kPrimaryArchitecture;
}


B_DEFINE_WEAK_ALIAS(__get_architecture, get_architecture);
B_DEFINE_WEAK_ALIAS(__get_primary_architecture, get_primary_architecture);
B_DEFINE_WEAK_ALIAS(__get_secondary_architectures, get_secondary_architectures);
B_DEFINE_WEAK_ALIAS(__get_architectures, get_architectures);
B_DEFINE_WEAK_ALIAS(__guess_architecture_for_path, guess_architecture_for_path);
