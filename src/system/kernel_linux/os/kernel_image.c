//------------------------------------------------------------------------------
//	Copyright (c) 2004, Bill Hayden
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		image.cpp
//	Authors:		Bill Hayden (hayden@haydentech.com)
//------------------------------------------------------------------------------

#include <stdio.h>

#include <image.h>
#include <dlfcn.h>


thread_id _kern_load_image(int32 argc, const char **argv, const char **envp)
{
	// FIXME: this should launch the app in parameter 1 of argv with the
	// arguments in argv and the environment variables in envp.  It
	// returns the launched application's main thread id.
	// The current app does NOT die, as with a straight exec.

	printf("load_image(): UNIMPLEMENTED\n");

	return B_ERROR;
}


image_id _kern_load_add_on(const char* path)
{
	void* hdll = dlopen(path, RTLD_LAZY);
	if( !hdll )
	{
		printf("load_add_on(): dlopen('%s', RTLD_LAZY) failed: %s\n", path, dlerror());
		return B_ERROR;
	}
	return (int)hdll;
}


status_t _kern_unload_add_on(image_id imageID)
{
	void* hdll = (void*)imageID;
	return dlclose(hdll) ? B_ERROR : B_OK;
}


status_t _kern_get_image_symbol(image_id imid, const char* name, int32 sclass, void** pptr)
{
	void* hdll = (void*)imid;
	const char* err = NULL;

	*pptr = dlsym(hdll, name);
	err = dlerror();
	if (err)
	{
		printf("_kern_get_image_symbol(): dlsym('%s') failed: %s\n", name, err);
		return B_BAD_IMAGE_ID;
	}

	return B_OK;
}


status_t
_kern_get_image_info(image_id id, image_info *info, size_t infoSize)
{
	printf("_kern_get_image_info(): UNIMPLEMENTED\n");
	return B_ERROR;
}


status_t
_kern_get_next_image_info(team_id teamID, int32 *cookie, image_info *info, size_t size)
{
	printf("_kern_get_next_image_info(): UNIMPLEMENTED\n");
	return B_ERROR;
}


void
_kern_clear_caches(void* address, size_t length, uint32 flags)
{
	printf("_kern_clear_caches(): UNIMPLEMENTED\n");
	return B_ERROR;
}
