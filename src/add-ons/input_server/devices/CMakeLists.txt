# Disabled add-ons — use Haiku kernel device paths that don't exist on Linux:
#   serial_mouse, easypen, wacom, tablet
# Superseded by libinput functionality merged into mouse add-on (touchpad/tablet/touchscreen/absolute pointer).
add_subdirectory(keyboard)
add_subdirectory(mouse)
#add_subdirectory(virtualkeyboard)
# libinput_device is now merged into the mouse add-on; build disabled
# add_subdirectory(libinput_device)
