list (APPEND SERVICES_LIST "${_SYSTEMD_SERVICES}")

set(SYSTEMD_SERVICES
  data/systemd/janus.service
  data/systemd/registrar.service
  data/systemd/mount_server.service
  data/systemd/app_server.service
  data/systemd/input_server.service
  data/systemd/notification_server.service
  data/systemd/deskbar.service
  data/systemd/tracker.service
  data/systemd/userbootscript.service
)

install(FILES ${SYSTEMD_SERVICES} DESTINATION /etc/systemd/system/)

install(FILES data/etc/systemd/journald.conf.d/vitruvian.conf DESTINATION /etc/systemd/journald.conf.d/)

#install(FILES data/etc/modules-load.d/befs.conf DESTINATION /etc/modules-load.d/)


# Boot scripts
ImageIncludeFile("data/system/boot/SetupEnvironment" "/system/boot")
ImageIncludeFile("data/system/boot/PostInstallScript" "/system/boot")
ImageIncludeDir("data/system/boot/post_install" "/system/boot/")
ImageIncludeDir("data/system/boot/first_login" "/system/boot/")

# System settings
ImageIncludeFile("data/system/settings/fresh_install" "/system/settings")

# Data files
ImageIncludeDir("data/system/data/fortunes" "/system/data/")
install(DIRECTORY "data/system/data/terminal_themes/" DESTINATION "/system/data/Terminal/Themes")
ImageIncludeDir("data/system/data/joysticks" "/system/data/")
ImageIncludeDir("data/system/data/network" "/system/data/")
ImageIncludeDir("data/system/data/licenses" "/system/data/")
ImageIncludeDir("data/system/data/fonts" "/system/data/")

# Default desktop wallpaper (V\OS logo, transparent PNG)
install(FILES "data/artwork/V_OS logo.png" DESTINATION "/system/data/artwork")

# Profile, inputrc, and profile.d scripts for Terminal
ImageIncludeFile("data/etc/profile" "/system/settings/etc")
ImageIncludeFile("data/etc/inputrc" "/system/settings/etc")
ImageIncludeDir("data/etc/profile.d" "/system/settings/etc/")

# User config boot scripts
ImageIncludeFile("data/config/boot/UserBootscript" "/home/config/settings/boot")
ImageIncludeFile("data/config/boot/UserSetupEnvironment.sample" "/home/config/settings/boot")

# User first-login flag
ImageIncludeFile("data/settings/first_login" "/home/config/settings")

# Deskbar menu entries descriptor file
ImageIncludeFile("src/data/deskbar/menu_entries" "/system/data/deskbar")

# Tracker New Templates: shipped as templates_attrs.tar (see base.cmake)
# and extracted with xattrs in postinst. Plain install(DIRECTORY) loses
# the BEOS:TYPE attrs that Tracker's New submenu needs.

# User guide and welcome launcher scripts
ImageIncludeFile("data/bin/userguide" "/bin")
ImageIncludeFile("data/bin/welcome" "/bin")

# Build files
ImageIncludeFile("data/develop/makefile-engine" "/etc")
ImageIncludeFile("data/develop/vitruvian.specs" "/etc")
