if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(addattr)
# add_subdirectory(desklink)  # media kit not yet implemented
add_subdirectory(keystore)
add_subdirectory(locale)
add_subdirectory(pc)
add_subdirectory(query)
add_subdirectory(rc)
# add_subdirectory(screen_blanker)  # screensaver kit not yet implemented
# add_subdirectory(screenmode)      # missing private graphics headers

UsePrivateHeaders(GLOBAL app interface libroot kernel shared storage support system tracker)

# No extra libs needed
Application(catarea    SOURCES catarea.c)
Application(chop       SOURCES chop.c)
Application(clear      SOURCES clear.c)
Application(driveinfo  SOURCES driveinfo.c)
Application(error      SOURCES error.c)
Application(finddir    SOURCES finddir.c)
Application(fortune    SOURCES fortune.c    RDEF fortune.rdef)
Application(get_driver_settings SOURCES get_driver_settings.cpp)
Application(hd         SOURCES hd.c)
Application(listarea   SOURCES listarea.c)
Application(listimage  SOURCES listimage.c)
Application(listport   SOURCES listport.c)
Application(listsem    SOURCES listsem.c)
Application(lsindex    SOURCES lsindex.cpp)
Application(mount      SOURCES mount.c)
Application(prio       SOURCES prio.c)
Application(printenv   SOURCES printenv.c)
Application(ps         SOURCES ps.c)
Application(release    SOURCES release.c)
Application(rescan     SOURCES rescan.c)
Application(rmattr     SOURCES rmattr.cpp)
Application(rmindex    SOURCES rmindex.cpp)
Application(system_time SOURCES system_time.cpp)
Application(unchop     SOURCES unchop.c)
Application(unmount    SOURCES unmount.c)
Application(vmstat     SOURCES vmstat.cpp)

# ncurses
Application(watch      SOURCES watch.c     LIBS ncurses)

# be (auto-linked, no extra libs)
Application(beep       SOURCES beep.cpp)
Application(catattr    SOURCES catattr.cpp)
Application(clipboard  SOURCES clipboard.cpp)
Application(diskimage  SOURCES diskimage.cpp)
Application(dpms       SOURCES dpms.cpp)
Application(draggers   SOURCES draggers.cpp)
Application(ffm        SOURCES ffm.cpp)
Application(iroster    SOURCES iroster.cpp)
Application(isvolume   SOURCES isvolume.cpp)
Application(listattr   SOURCES listattr.cpp)
Application(listfont   SOURCES listfont.cpp)
Application(listres    SOURCES listres.cpp)
Application(message    SOURCES message.cpp)
Application(mkindex    SOURCES mkindex.cpp)
Application(modifiers  SOURCES modifiers.cpp)
Application(mvattr     SOURCES mvattr.cpp)
Application(quit       SOURCES quit.cpp)
Application(roster     SOURCES roster.cpp)
Application(setversion SOURCES setversion.cpp)
Application(shutdown   SOURCES shutdown.cpp)
Application(trash      SOURCES trash.cpp)
Application(version    SOURCES version.cpp)
Application(WindowShade SOURCES WindowShade.cpp)

# be + shared
# Application(launch_roster SOURCES launch_roster.cpp LIBS shared)  # BLaunchRoster not yet implemented

# be + supc++ (supc++ auto-linked on Linux)
Application(alert          SOURCES alert.cpp)
Application(dstcheck       SOURCES dstcheck.cpp       LIBS localestub  RDEF dstcheck.rdef)
#Application(eject          SOURCES eject.cpp)  # needs device/scsi.h (not yet ported)
Application(hey            SOURCES hey.cpp             RDEF hey.rdef)
Application(reindex        SOURCES reindex.cpp)
Application(resattr        SOURCES resattr.cpp)
Application(screeninfo     SOURCES screeninfo.cpp)
Application(setcontrollook SOURCES setcontrollook.cpp)
Application(setdecor       SOURCES setdecor.cpp)
Application(settype        SOURCES settype.cpp)
Application(spybmessage    SOURCES spybmessage.cpp)
Application(urlwrapper     SOURCES urlwrapper.cpp      RDEF urlwrapper.rdef)

# be + bnetapi
#Application(open           SOURCES open.cpp)
#Application(waitfor        SOURCES waitfor.cpp)
Application(checkitout     SOURCES checkitout.cpp       LIBS tracker  RDEF checkitout.rdef)

# be + stdc++ (stdc++ auto-linked on Linux)
Application(copyattr       SOURCES copyattr.cpp)
Application(diff_zip       SOURCES diff_zip.cpp)
Application(mimeset        SOURCES mimeset.cpp          RDEF mimeset.rdef)
Application(mountvolume    SOURCES mountvolume.cpp      RDEF mountvolume.rdef)
Application(setmime        SOURCES setmime.cpp          RDEF setmime.rdef)
Application(sysinfo        SOURCES sysinfo.cpp)
Application(xres           SOURCES xres.cpp)

# be + translation
Application(notify         SOURCES notify.cpp           LIBS translation icon  RDEF notify.rdef)
Application(translate      SOURCES translate.cpp        LIBS translation)

# be + tracker + localestub
Application(filepanel      SOURCES filepanel.cpp        LIBS tracker localestub)

# be + media (media kit not yet implemented)
#Application(installsound   SOURCES installsound.cpp     LIBS media)
#Application(setvolume      SOURCES setvolume.cpp        LIBS media)

# be + package (package kit not yet implemented)
#Application(findpaths      SOURCES findpaths.cpp        LIBS package)

else()

add_subdirectory(rc)

BuildModeApplication(resattr SOURCES resattr.cpp)
BuildModeApplication(xres SOURCES xres.cpp)

endif()
