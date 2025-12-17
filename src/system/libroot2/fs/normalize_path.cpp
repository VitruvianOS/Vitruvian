/*
 * Copyright 2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <StorageDefs.h>

#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>


// Append 'len' bytes from src into dst buffer of capacity B_PATH_NAME_LENGTH.
// dst_len points to current length; NULL-terminates on success.
// Returns 0 on success, B_NAME_TOO_LONG on overflow, -EINVAL for bad args.
static status_t
stack_append(char dst[B_PATH_NAME_LENGTH], size_t *dst_len, const char *src, size_t len)
{
	if (!dst || !dst_len || (!src && len != 0))
		return B_BAD_VALUE;

	// check remaining capacity (leave room for NUL)
	if (*dst_len > (size_t)B_PATH_NAME_LENGTH - 1)
		return B_NAME_TOO_LONG;
	if (len > (size_t)B_PATH_NAME_LENGTH - 1 - *dst_len)
		return B_NAME_TOO_LONG;

	// copy and terminate
	memcpy(dst + *dst_len, src, len);
	*dst_len += len;
	dst[*dst_len] = '\0';
	return 0;
}


// Ensure trailing slash exists in dst unless dst == "/"
static status_t
stack_ensure_trailing_slash(char dst[B_PATH_NAME_LENGTH], size_t *dst_len)
{
	if (!dst || !dst_len)
		return -EINVAL;
	if (*dst_len == 1 && dst[0] == '/')
		return 0;
	if (*dst_len == 0)
		return stack_append(dst, dst_len, "/", 1);
	if (dst[*dst_len - 1] == '/')
		return 0;
	return stack_append(dst, dst_len, "/", 1);
}


// Pop last component from dst; keep "/" if root
static void
stack_pop_last_component(char dst[B_PATH_NAME_LENGTH], size_t *dst_len)
{
	if (!dst || !dst_len || *dst_len == 0)
		return;

	// if root or single byte, make sure it's "/"
	if (*dst_len <= 1) {
		dst[0] = '/';
		dst[1] = '\0';
		*dst_len = 1;
		return;
	}

	// if trailing slash, drop it first
	if (dst[*dst_len - 1] == '/') {
		dst[*dst_len - 1] = '\0';
		(*dst_len)--;
	}

	// find previous slash
	char *p = strrchr(dst, '/');
	if (!p) {
		// fallback to root
		dst[0] = '/';
		dst[1] = '\0';
		*dst_len = 1;
		return;
	}

	if (p == dst) {
		// only leading slash remains
		dst[1] = '\0';
		*dst_len = 1;
	} else {
		size_t new_len = (size_t)(p - dst);
		dst[new_len] = '\0';
		*dst_len = new_len;
	}
}


// Normalize userPath into caller buffer. No heap used, result bounded by B_PATH_NAME_LENGTH.
// Behavior:
//  - collapse duplicate slashes
//  - handle "." and ".."
//  - resolve relative paths against getcwd()
//  - do NOT follow/expand symlinks
// Returns 0 on success, negative errno on failure.
extern "C" status_t
_kern_normalize_path(const char* userPath, bool _ignored, char* buffer)
{
	if (userPath == NULL || buffer == NULL)
		return B_BAD_VALUE;

	const char *path = userPath;
	bool is_absolute = (path[0] == '/');

	// getcwd for relative paths
	char cwd_stack[B_PATH_NAME_LENGTH];
	const char *cwd = NULL;
	if (!is_absolute) {
		if (getcwd(cwd_stack, sizeof(cwd_stack)) == NULL)
			return -errno;
		cwd = cwd_stack;
	}

	// temporary stack buffer to build result
	char tmp[B_PATH_NAME_LENGTH];
	size_t tmp_len = 0;
	tmp[0] = '\0';

	if (is_absolute) {
		if (stack_append(tmp, &tmp_len, "/", 1) != 0)
			return B_NAME_TOO_LONG;
	} else {
		size_t cwd_len = strlen(cwd);
		if (cwd_len >= B_PATH_NAME_LENGTH) return B_NAME_TOO_LONG;
		if (stack_append(tmp, &tmp_len, cwd, cwd_len) != 0)
			return B_NAME_TOO_LONG;
		// ensure trailing slash for easier appends
		if (!(tmp_len == 1 && tmp[0] == '/') && tmp[tmp_len - 1] != '/') {
			if (stack_append(tmp, &tmp_len, "/", 1) != 0)
				return B_NAME_TOO_LONG;
		}
	}

	// iterate components in path
	size_t i = 0;
	size_t path_len = strlen(path);

	while (i < path_len) {
		// skip repeated slashes
		while (i < path_len && path[i] == '/')
			i++;

		if (i >= path_len)
			break;

		// find end of component
		size_t j = i;
		while (j < path_len && path[j] != '/')
			j++;

		size_t comp_len = j - i;

		// sanity check
		if (comp_len >= B_PATH_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		// special cases
		if (comp_len == 1 && path[i] == '.') {
			// no-op
		} else if (comp_len == 2 && path[i] == '.' && path[i+1] == '.') {
			// parent
			stack_pop_last_component(tmp, &tmp_len);
		} else if (comp_len == 0) {
			// ignore defensively
		} else {
			// ensure separator if needed
			if (!(tmp_len == 1 && tmp[0] == '/') && tmp[tmp_len - 1] != '/') {
				if (stack_append(tmp, &tmp_len, "/", 1) != 0)
					return B_NAME_TOO_LONG;
			}
			// append component bytes
			if (comp_len > (size_t)B_PATH_NAME_LENGTH - 1 - tmp_len)
				return B_NAME_TOO_LONG;
			if (stack_append(tmp, &tmp_len, path + i, comp_len) != 0)
				return B_NAME_TOO_LONG;
		}

		i = j;
	}

	// if empty, set to "/"
	if (tmp_len == 0) {
		if (stack_append(tmp, &tmp_len, "/", 1) != 0)
			return B_NAME_TOO_LONG;
	}

	// remove trailing slash unless root
	if (tmp_len > 1 && tmp[tmp_len - 1] == '/') {
		tmp[tmp_len - 1] = '\0';
		tmp_len--;
	}

	// final safety
	if (tmp_len >= B_PATH_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	// copy to caller buffer (assume B_PATH_NAME_LENGTH sized), always NUL-terminate
	size_t copy_len = tmp_len;
	if (copy_len > (size_t)B_PATH_NAME_LENGTH - 1) copy_len = B_PATH_NAME_LENGTH - 1;
	memcpy(buffer, tmp, copy_len);
	buffer[copy_len] = '\0';

	return 0;
}
