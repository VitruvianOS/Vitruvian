# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Link an app as a shared object that is still executable (PT_INTERP, -e _start),
# so the Deskbar can dlopen() it as a replicant. glibc refuses PIE executables.
# In debian/rules:
#   export VOS_RUNNABLE_ADDON := 1
#   include /usr/share/dh-vos/runnable-addon.mk
# The vos_makefile_engine buildsystem then links TYPE=SHARED and makes the
# interpreter object itself. Other builds pass $(RUNNABLE_LDFLAGS) to their
# link and depend on runnable-addon-interp / runnable-addon-clean.

DEB_HOST_ARCH ?= $(shell dpkg-architecture -qDEB_HOST_ARCH)
INTERP_amd64 = /lib64/ld-linux-x86-64.so.2
INTERP_arm64 = /lib/ld-linux-aarch64.so.1
INTERP_riscv64 = /lib/ld-linux-riscv64-lp64d.so.1

RUNNABLE_INTERP_O := $(CURDIR)/runnable_addon_interp.o
RUNNABLE_LDFLAGS := -nostartfiles -Wl,-e,_start -Wl,-z,noexecstack \
	$(RUNNABLE_INTERP_O) \
	$(shell cc -print-file-name=crti.o) $(shell cc -print-file-name=Scrt1.o) \
	$(shell cc -print-file-name=crtbeginS.o) \
	$(shell cc -print-file-name=crtendS.o) $(shell cc -print-file-name=crtn.o)

export DEB_CFLAGS_APPEND += -fPIC
export DEB_CXXFLAGS_APPEND += -fPIC
ifdef VOS_RUNNABLE_ADDON
export DEB_LDFLAGS_APPEND += $(RUNNABLE_LDFLAGS)
endif

runnable-addon-interp:
	# ld emits PT_INTERP for a -shared link only from an input object.
	printf '%s\n' '.section .interp,"a"' '.asciz "$(INTERP_$(DEB_HOST_ARCH))"' \
		'.section .note.GNU-stack,"",%progbits' > runnable_addon_interp.S
	cc -c runnable_addon_interp.S -o runnable_addon_interp.o

runnable-addon-clean:
	rm -f runnable_addon_interp.S runnable_addon_interp.o

.PHONY: runnable-addon-interp runnable-addon-clean
