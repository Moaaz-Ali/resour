###############################################################################
###############################################################################
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include make/defines.mk

REALSUBDIRS = gnbd-kernel/src gfs-kernel/src/gfs \
	      cman/lib ccs cman dlm group fence gfs gfs2 gnbd rgmanager

SUBDIRS = $(filter-out \
	  $(if ${without_gnbd},gnbd-kernel/src) \
	  $(if ${without_gfs},gfs-kernel/src/gfs) \
	  $(if ${without_cman},cman/lib) \
	  $(if ${without_ccs},ccs) \
	  $(if ${without_cman},cman) \
	  $(if ${without_dlm},dlm) \
	  $(if ${without_group},group) \
	  $(if ${without_fence},fence) \
	  $(if ${without_gfs},gfs) \
	  $(if ${without_gfs2},gfs2) \
	  $(if ${without_gnbd},gnbd) \
	  $(if ${without_rgmanager},rgmanager) \
	  , $(REALSUBDIRS))

all: scripts ${SUBDIRS}

# Fix scripts permissions
scripts:
	chmod 755 ${BUILDDIR}/scripts/*.pl ${BUILDDIR}/scripts/define2var

${SUBDIRS}:
	[ -n "${without_$@}" ] || ${MAKE} -C $@ all

# Kernel

gnbd-kernel: gnbd-kernel/src
gfs-kernel: gfs-kernel/src/gfs

# Dependencies

cman/lib:
ccs: cman/lib
cman: ccs
dlm:
group: ccs dlm
fence: group dlm
gfs:
gfs2:
gnbd: cman/lib
rgmanager: ccs dlm

install: all
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

uninstall:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

clean:
	set -e && for i in ${REALSUBDIRS}; do ${MAKE} -C $$i $@; done

distclean: clean
	rm -f make/defines.mk
	rm -f *tar.gz
	rm -rf build

.PHONY: scripts ${REALSUBDIRS}
