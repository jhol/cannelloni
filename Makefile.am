##
## This file is part of the cannelloni project.
##
## Copyright (C) 2023 NVIDIA Corporation <jholdsworth@nvidia.com>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, see <http://www.gnu.org/licenses/>.
##

dist_noinst_SCRIPTS = autogen.sh

SUBDIRS = \
	firmware \
	.

bin_PROGRAMS = cannelloni

cannelloni_SOURCES = \
	src/cannelloni.c \
	src/config.h \
	src/ezusb.c \
	src/ezusb.h

cannelloni_LDADD = $(LIBUSB_1_0_LIBS)

.PHONY: dist-changelog

dist-hook: dist-changelog

dist-changelog:
	$(AM_V_at)if test ! -d '$(top_srcdir)/.git'; then \
		cp -f '$(top_srcdir)/ChangeLog' "$(top_distdir)/ChangeLog"; \
	elif git -C '$(top_srcdir)' log >.ChangeLog.tmp; then \
		mv -f .ChangeLog.tmp "$(top_distdir)/ChangeLog"; \
	else \
		rm -f .ChangeLog.tmp; exit 1; \
	fi
