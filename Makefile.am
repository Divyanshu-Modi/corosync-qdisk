#
# Copyright (c) 2025 IBM, Inc.
#
# Authors: Thomas Jones (thomas.jones@ibm.com)
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

TARFILE			= $(PACKAGE_NAME)-$(VERSION).tar.gz

EXTRA_DIST = autogen.sh \
             corosync-qdisk.service.in

AM_CPPFLAGS     = -include ${top_builddir}/config.h -I ${top_builddir} -I ${top_srcdir}
ACLOCAL_AMFLAGS = -I m4

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure depcomp \
                       config.guess config.sub missing install-sh \
                       autoheader automake autoconf \
                       autoscan.log configure.scan ltmain.sh

dist_doc_DATA      = LICENSE

systemdconfdir	   = $(SYSTEMDDIR)
systemdconf_DATA   = corosync-qdisk.service

all-local: $(systemdconf_DATA)
clean-local:
	rm -rf $(systemdconf_DATA)

bin_PROGRAMS  =
sbin_PROGRAMS = corosync-qdisktool corosync-qdisk

corosync_qdisk_SOURCES = \
qdiskd/main.c \
qdiskd/algorithm.c \
qdiskd/algorithm.h \
qdiskd/cmap.c \
qdiskd/cmap.h \
qdiskd/log.c \
qdiskd/log.h \
qdiskd/vquorum.c \
qdiskd/vquorum.h
corosync_qdisk_CFLAGS = $(cmap_CFLAGS) $(votequorum_CFLAGS) $(quorum_CFLAGS) $(corosync_common_CFLAGS) $(libqb_CFLAGS) $(systemd_CFLAGS)
corosync_qdisk_LDADD  = $(cmap_LIBS) $(votequorum_LIBS) $(quorum_LIBS) $(corosync_common_LIBS) $(libqb_LIBS) $(systemd_LIBS) \
                libpersistent_reserve.la

noinst_LTLIBRARIES	 = libpersistent_reserve.la

libpersistent_reserve_la_SOURCES = \
persistent_reserve/persistent_reserve.h \
persistent_reserve/persistent_reserve.c \
persistent_reserve/helpers.h \
persistent_reserve/nvme_device.c \
persistent_reserve/nvme_device.h \
persistent_reserve/device.c \
persistent_reserve/device.h \
persistent_reserve/scsi_device.c \
persistent_reserve/scsi_device.h

corosync_qdisktool_SOURCES = persistent_reserve/qdisktool.c
corosync_qdisktool_LDADD = libpersistent_reserve.la

# release/versioning
BUILT_SOURCES	= .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@


%: %.in Makefile
	rm -f $@-t $@
	cat $< | $(SED) \
		-e 's#@''SBINDIR@#$(sbindir)#g' \
		-e 's#@''BINDIR@#$(bindir)#g' \
		-e 's#@''SYSCONFDIR@#$(sysconfdir)#g' \
		-e 's#@''INITCONFIGDIR@#$(INITCONFIGDIR)#g' \
		-e 's#@''INITDDIR@#$(INITDDIR)#g' \
		-e 's#@''LOCALSTATEDIR@#$(localstatedir)#g' \
		-e 's#@''BASHPATH@#${BASHPATH}#g' \
	    > $@-t
	mv $@-t $@
