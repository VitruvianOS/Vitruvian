# Haiku's jam (make redux), imported from haiku/buildtools jam/.
# Portable C — no Be API, so no Application() / LinuxBuildCompatibility.h.
# jamgram.c and jambase.c are committed generated files, as upstream does;
# the upstream Makefile compiles them as-is and does not regenerate them.

set(JAM_SOURCES
	builtins.c
	command.c
	compile.c
	execunix.c
	execvms.c
	expand.c
	filent.c
	fileos2.c
	fileunix.c
	filevms.c
	glob.c
	hash.c
	headers.c
	hcache.c
	jam.c
	jambase.c
	jamgram.c
	jcache.c
	lists.c
	make.c
	make1.c
	newstr.c
	option.c
	parse.c
	pathunix.c
	pathvms.c
	regexp.c
	rules.c
	scan.c
	search.c
	timestamp.c
	variable.c
)

add_executable(jam ${JAM_SOURCES})

# Same OPT_* flags the upstream jam Makefile uses on Unix.
target_compile_definitions(jam PRIVATE
	OPT_HEADER_CACHE_EXT
	OPT_JAMFILE_CACHE_EXT
	OPT_JAM_TARGETS_VARIABLE_EXT
)
