##
## This file is part of the cannelloni project.
##
## Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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

# We require at least autoconf 2.63 (AC_INIT format changed there).
AC_PREREQ([2.63])

AC_INIT([cannelloni], [0.1.0], [],
	[cannelloni], [https://github.com/yomboprime/cannelloni])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autostuff])
AC_CONFIG_HEADERS([config.h])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define subdir-objects])
AM_SILENT_RULES([yes])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

# Required for per-target flags or subdir-objects with C sources.
AM_PROG_CC_C_O

#
# C Compiler
#

# Get compiler version.
CN_PROG_VERSION([$CC], [cn_cc_version])

# Enable the C11 standard if possible, and enforce the use
# of CN_API to explicitly mark all public API functions.
CN_EXTRA_CFLAGS=
CN_CHECK_COMPILE_FLAGS([CN_EXTRA_CFLAGS], [C11], [-std=c11 -c11 -AC11 -qlanglvl=extc11])
CN_CHECK_COMPILE_FLAGS([CN_EXTRA_CFLAGS], [visibility], [-fvisibility=hidden])

CN_ARG_ENABLE_WARNINGS([CN_WFLAGS], [-Wall], [-Wall -Wextra -Wmissing-prototypes])

AM_CONDITIONAL([WIN32], [test -z "${host_os##mingw*}" || test -z "${host_os##cygwin*}"])

#
# Dependencies
#

AC_LANG([C])

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# libusb-1.0

# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
# This means libusb-1.0 is always available; no need to check for it.
# On Windows, require the latest version we can get our hands on,
# until the new generic event handling has seen an official release.
AS_CASE([$host_os],
	[freebsd*], [cn_libusb_req='libusb-1.0' cn_have_libusb=yes],
	[mingw*], [cn_libusb_req='libusb-1.0 >= 1.0.20'],
	[cn_libusb_req='libusb-1.0 >= 1.0.16'])
PKG_CHECK_MODULES([LIBUSB_1_0], [$cn_libusb_req])
cn_libusb_version=`$PKG_CONFIG --modversion "$cn_libusb_req" 2>&AS_MESSAGE_LOG_FD`

#
# SDCC
#

# On Fedora, all sdcc tools have an 'sdcc-' prefix in the filename.
AC_CHECK_PROGS([SDCC], [sdcc sdcc-sdcc])
AS_IF([test "x$SDCC" = x],
	[AC_MSG_ERROR([cannot find sdcc.])])

AC_CHECK_PROGS([SDAR], [sdar sdcc-sdar])
AS_IF([test "x$SDAR" = x],
	[AC_MSG_ERROR([cannot find sdar.])])

cn_sdcc_version=`$SDCC --version | sed -n 's/.* \([[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\) .*/\1/p;q' 2>&AS_MESSAGE_LOG_FD`
AS_VERSION_COMPARE([$cn_sdcc_version], [3.4.0],
	[AC_MSG_ERROR([At least version 3.4.0 of sdcc is required.])])

AC_CHECK_PROGS([OBJCOPY], [objcopy])
AC_CHECK_PROGS([MAKEBIN], [makebin sdcc-makebin])
AS_IF([test "x$OBJCOPY$MAKEBIN" = x],
	[AC_MSG_ERROR([cannot find objcopy or makebin.])])

AM_CONDITIONAL([FOUND_OBJCOPY], [test "x$OBJCOPY" != x])
AM_CONDITIONAL([FOUND_MAKEBIN], [test "x$MAKEBIN" != x])

# The sdcc 8051 assembler binary has/had different names:
# Newer sdcc, Debian / Ubuntu: sdas8051
# Newer sdcc, Fedora: sdcc-sdas8051
# Older sdcc: asx8051
AC_CHECK_PROGS([SDAS8051], [sdas8051 sdcc-sdas8051 asx8051])
AS_IF([test "x$SDAS8051" = x],
	[AC_MSG_ERROR([cannot find sdas8051.])])

# Set up the version defines.
CN_PKG_VERSION_SET([CN_PACKAGE_VERSION], [AC_PACKAGE_VERSION])

AC_CONFIG_FILES([
	Makefile
	firmware/Makefile
])
AC_OUTPUT

cat >&AS_MESSAGE_FD <<_EOF

cannellonni configuration summary:
 - Package version................. $PACKAGE_VERSION
 - Prefix.......................... $prefix
 - Building on..................... $build
 - Building for.................... $host

Compile configuration:
 - C compiler...................... $CC
 - C compiler version.............. $cn_cc_version
 - C compiler flags................ $CFLAGS
 - Additional C compiler flags..... $CN_EXTRA_CFLAGS
 - C compiler warnings............. $CN_WFLAGS
 - Linker flags.................... $LDFLAGS

Library dependencies:
 - libusb-1.0...................... $cn_libusb_version

8051 Compile configuration:
 - C compiler...................... $SDCC
 - C compiler version.............. $cn_sdcc_version

_EOF
