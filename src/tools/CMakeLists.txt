UsePrivateHeaders(GLOBAL app storage support shared)

#Application(cpuidtool                 SOURCES cpuidtool.c)
#Application(create_image              SOURCES create_image.cpp)
#Application(data_to_source            SOURCES data_to_source.cpp)
#Application(elf2aout                  SOURCES elf2aout.c)
#Application(exec                      SOURCES exec.c)
#Application(generate_attribute_stores SOURCES generate_attribute_stores.cpp)
#Application(rm_attrs                  SOURCES rm_attrs.cpp)

add_subdirectory(cppunit)
#add_subdirectory(hvif2png)
#add_subdirectory(locale)      # missing HashMapCatalog.cpp
#add_subdirectory(MsgSpy)
#add_subdirectory(restest)
#add_subdirectory(translation)
#add_subdirectory(unflatten)
