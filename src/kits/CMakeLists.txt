find_package(ICU REQUIRED COMPONENTS i18n uc dt in io)

AddOn(
	be
	SHARED

	LIBS root icon ${ICU_LIBRARIES} agg ${ZLIB_LIBRARIES} pthread localestub shared systemd
)

UsePrivateHeaders(be app interface kernel print shared locale libroot shared system)

target_compile_options(be PRIVATE -std=c++17)
# Debug-only vref tracing, off by default: set VREF_TRACE=/dir and re-add this.
# target_compile_definitions(be PRIVATE ENABLE_VREF_TRACKING)

# Both the network kit (libnm) and the bluetooth kit (BlueZBackend.cpp, via
# gio) talk D-Bus through glib. Declared once here rather than in each kit's
# own CMakeLists.txt so it isn't pulled in twice; PRIVATE to "be" is load-
# bearing -- it keeps GLib/GDBus headers out of src/apps and src/preferences,
# which is what stops those layers from calling D-Bus directly and bypassing
# NMBackend/BlueZBackend.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(GIO REQUIRED gio-2.0)
	if(GIO_FOUND)
		target_include_directories(be PRIVATE ${GIO_INCLUDE_DIRS})
		target_link_libraries(be PRIVATE ${GIO_LIBRARIES})
		target_compile_options(be PRIVATE ${GIO_CFLAGS_OTHER})
	endif()
endif()

AddOn(
	be_bootstrap
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders(be_bootstrap interface app support)

add_subdirectory(app)
add_subdirectory(bluetooth)
add_subdirectory(game)
add_subdirectory(interface)
add_subdirectory(locale)
add_subdirectory(media2)
add_subdirectory(network)
#add_subdirectory(print)
add_subdirectory(opengl)
add_subdirectory(screensaver)
add_subdirectory(shared)
add_subdirectory(storage)
add_subdirectory(support)
add_subdirectory(textencoding)
add_subdirectory(translation)
add_subdirectory(tracker)
DoCatalogs("x-vnd.Haiku-libbe" kits)
