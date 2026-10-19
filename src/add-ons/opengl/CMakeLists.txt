# Order here is build order only; runtime preference is enforced by
# BGLView::_LoadRenderer (kits/opengl/GLView.cpp), not by this list.
add_subdirectory(mesa_egl)
add_subdirectory(mesa_surfaceless)
