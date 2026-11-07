install(FILES vos.pm
	DESTINATION /usr/share/perl5/Debian/Debhelper/Sequence
	COMPONENT dh-vos)
install(FILES vos_makefile_engine.pm
	DESTINATION /usr/share/perl5/Debian/Debhelper/Buildsystem
	COMPONENT dh-vos)
install(PROGRAMS dh_vosattrs dh_vosmenu dh_vosstrip dh_vosshlibdeps
	DESTINATION /usr/bin
	COMPONENT dh-vos)
install(FILES runnable-addon.mk
	DESTINATION /usr/share/dh-vos
	COMPONENT dh-vos)
