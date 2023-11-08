# Makefile.am - build rules
# Copyright 2005-2023 Bas Wijnen <wijnen@debian.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I m4

# Rules for versions (current:revision:age), from libtool manual:
# 1. Start with version information of ‘0:0:0’ for each libtool library.
# 2. Update the version information only immediately before a public release of your software. More frequent updates are unnecessary, and only guarantee that the current interface number gets larger faster.
# 3. If the library source code has changed at all since the last update, then increment revision (‘c:r:a’ becomes ‘c:r+1:a’).
# 4. If any interfaces have been added, removed, or changed since the last update, increment current, and set revision to 0.
# 5. If any interfaces have been added since the last public release, then increment age.
# 6. If any interfaces have been removed or changed since the last public release, then set age to 0. 
VERSION_CURRENT = 0
VERSION_REVISION = 0
VERSION_AGE = 0

webloopdir = $(prefix)/include
webloopsubdir = $(prefix)/include/webloop
pkgdir = $(libdir)/pkgconfig

lib_LTLIBRARIES = libwebloop.la

pkg_DATA = webloop.pc
#noinst_DATA = doc

EXTRA_DIST = autogen.sh symbol-versions webloop.pc.in

JUNK = \
	aclocal.m4 \
	compile \
	config.guess \
	config.sub \
	configure \
	configure~ \
	depcomp \
	install-sh \
	ltmain.sh \
	m4 \
	Makefile.in \
	missing

webloop_HEADERS = \
	include/webloop.hh
webloopsub_HEADERS = \
	include/webloop/coroutine.hh \
	include/webloop/fhs.hh \
	include/webloop/loop.hh \
	include/webloop/network.hh \
	include/webloop/tools.hh \
	include/webloop/url.hh \
	include/webloop/webobject.hh \
	include/webloop/websocketd.hh

libwebloop_la_SOURCES = $(webloop_HEADERS) $(webloopsub_HEADERS) \
	src/coroutine.cc \
	src/fhs.cc \
	src/loop.cc \
	src/network.cc \
	src/tools.cc \
	src/url.cc \
	src/webobject.cc \
	src/websocketd.cc

libwebloop_la_CPPFLAGS = -Iinclude -Wall -Werror -ggdb3 -std=c++23
libwebloop_la_LDFLAGS = -Wl,--version-script -Wl,symbol-versions -version-info $(VERSION_CURRENT):$(VERSION_REVISION):$(VERSION_AGE) -Wl,--as-needed
libwebloop_la_LIBADD = -ldl -lpthread
libwebloop_la_DEPENDENCIES = symbol-versions

#doc:
#	doxygen config.doxygen
#	$(MAKE) -C doc/latex

# For some reason, files in here are created write-protected.
# So use -f while removing them.
#clean-local:
#	rm -rf doc

autoclean: maintainer-clean
 ifneq ($(wildcard $(JUNK)),)
	rm -r $(wildcard $(JUNK))
 endif

#.PHONY: doc
