# Disabled add-ons — use Haiku kernel device paths that don't exist on Linux:
#   serial_mouse, easypen, wacom, tablet
# Superseded by libinput functionality merged into mouse add-on (touchpad/tablet/touchscreen/absolute pointer).
add_subdirectory(keyboard)
add_subdirectory(mouse)
add_subdirectory(gamepad)
#add_subdirectory(virtualkeyboard)
