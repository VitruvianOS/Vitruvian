/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef HWACCEL_GL_RENDERER_PRIORITY_H
#define HWACCEL_GL_RENDERER_PRIORITY_H


#include <StringList.h>


// OpenGL renderer priority list shared by the hwaccel preflet and BGLView's
// add-on loader. Stored as a flattened BMessage at
// ~/config/settings/opengl/render_priority: one string per add-on file name
// (key "render_priority"), most preferred first. Overrides directory order.
namespace GLRendererPriority {

// Settings file, relative to B_USER_SETTINGS_DIRECTORY.
extern const char* kSettingsPath;

// Key holding the B_STRING_TYPE array inside the flattened message.
extern const char* kPriorityKey;

// Renderer add-on directory BGLView loads from.
extern const char* kRendererDir;

// Fills 'renderers' with the base names of every regular file in
// kRendererDir, in directory order (the order BGLView would try them
// without a priority file). Missing directory yields B_OK and an empty
// list.
status_t GetInstalledRenderers(BStringList& renderers);

// Reads kSettingsPath into 'priority'. An absent or unreadable file is
// not an error: B_OK with 'priority' left empty means "no priority set,
// fall back to directory order".
status_t Load(BStringList& priority);

// Writes 'priority' to kSettingsPath, creating the enclosing directory
// if needed.
status_t Save(const BStringList& priority);

// Merges the saved priority with what is currently installed: entries
// from the saved list that still exist in kRendererDir first (saved
// order), then installed renderers not covered by the list (directory
// order). This is the list the preflet displays and persists.
status_t GetEffective(BStringList& priority);

} // namespace GLRendererPriority


#endif // HWACCEL_GL_RENDERER_PRIORITY_H
