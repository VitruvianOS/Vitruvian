set(BIN_DIRECTORY
	# subdirectory-based
	addattr
	keystore
	linkcatkeys
	dumpcatalog
	pc
	query
	rc

	# no extra libs
	catarea
	chop
	clear
	driveinfo
	error
	finddir
	fortune
	get_driver_settings
	hd
	listarea
	listimage
	listport
	listsem
	lsindex
	mount
	prio
	printenv
	ps
	release
	rescan
	rmattr
	rmindex
	system_time
	unchop
	unmount
	vmstat

	# ncurses
	watch

	# be
	beep
	catattr
	clipboard
	diskimage
	dpms
	draggers
	ffm
	iroster
	isvolume
	listattr
	listfont
	listres
	message
	mkindex
	modifiers
	mvattr
	quit
	roster
	setversion
	shutdown
	trash
	version
	WindowShade

	# be + supc++
	alert
	dstcheck
	hey
	reindex
	resattr
	screeninfo
	setcontrollook
	setdecor
	settype
	spybmessage
	urlwrapper

	# be + bnetapi
	open
	checkitout

	# be + stdc++
	copyattr
	diff_zip
	mimeset
	mountvolume
	setmime
	sysinfo
	xres

	# be + translation
	notify
	translate

	# be + tracker
	filepanel
)
ImageInclude("/bin" ${BIN_DIRECTORY})
