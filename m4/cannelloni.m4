##
## This file is part of the cannelloni project.
##
## Copyright (C) 2009 Openismus GmbH
## Copyright (C) 2015 Daniel Elstner <daniel.kitta@gmail.com>
## Copyright (C) 2023 NVIDIA Corporation <jholdsworth@nvidia.com>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

## CN_APPEND(var-name, [list-sep,] element)
##
## Append the shell word <element> to the shell variable named <var-name>,
## prefixed by <list-sep> unless the list was empty before appending. If
## only two arguments are supplied, <list-sep> defaults to a single space
## character.
##
AC_DEFUN([CN_APPEND],
[dnl
m4_assert([$# >= 2])[]dnl
$1=[$]{$1[}]m4_if([$#], [2], [[$]{$1:+' '}$2], [[$]{$1:+$2}$3])[]dnl
])

## _CN_PKG_VERSION_SET(var-prefix, pkg-name, tag-prefix, base-version, major, minor, [micro])
##
m4_define([_CN_PKG_VERSION_SET],
[dnl
m4_assert([$# >= 6])[]dnl
$1=$4
cn_git_deps=
# Check if we can get revision information from git.
cn_head=`git -C "$srcdir" rev-parse --verify --short HEAD 2>&AS_MESSAGE_LOG_FD`

AS_IF([test "$?" = 0 && test "x$cn_head" != x], [dnl
	test ! -f "$srcdir/.git/HEAD" \
		|| cn_git_deps="$cn_git_deps \$(top_srcdir)/.git/HEAD"

	cn_head_name=`git -C "$srcdir" rev-parse --symbolic-full-name HEAD 2>&AS_MESSAGE_LOG_FD`
	AS_IF([test "$?" = 0 && test -f "$srcdir/.git/$cn_head_name"],
		[cn_git_deps="$cn_git_deps \$(top_srcdir)/.git/$cn_head_name"])

	# Append the revision hash unless we are exactly on a tagged release.
	git -C "$srcdir" describe --match "$3$4" \
		--exact-match >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD \
		|| $1="[$]$1-git-$cn_head"
])
# Use $(wildcard) so that things do not break if for whatever
# reason these files do not exist anymore at make time.
AS_IF([test -n "$cn_git_deps"],
	[CN_APPEND([CONFIG_STATUS_DEPENDENCIES], ["\$(wildcard$cn_git_deps)"])])
AC_SUBST([CONFIG_STATUS_DEPENDENCIES])[]dnl
AC_SUBST([$1])[]dnl
dnl
AC_DEFINE([$1_MAJOR], [$5], [Major version number of $2.])[]dnl
AC_DEFINE([$1_MINOR], [$6], [Minor version number of $2.])[]dnl
m4_ifval([$7], [AC_DEFINE([$1_MICRO], [$7], [Micro version number of $2.])])[]dnl
AC_DEFINE_UNQUOTED([$1_STRING], ["[$]$1"], [Version of $2.])[]dnl
])

## CN_PKG_VERSION_SET(var-prefix, version-triple)
##
## Set up substitution variables and macro definitions for the package
## version components. Derive the version suffix from the repository
## revision if possible.
##
## Substitutions: <var-prefix>
## Macro defines: <var-prefix>_{MAJOR,MINOR,MICRO,STRING}
##
AC_DEFUN([CN_PKG_VERSION_SET],
[dnl
m4_assert([$# >= 2])[]dnl
_CN_PKG_VERSION_SET([$1],
	m4_defn([AC_PACKAGE_NAME]),
	m4_defn([AC_PACKAGE_TARNAME])[-],
	m4_expand([$2]),
	m4_unquote(m4_split(m4_expand([$2]), [\.])))
])

## CN_PROG_VERSION(program, sh-var)
##
## Obtain the version of <program> and store it in <sh-var>.
##
AC_DEFUN([CN_PROG_VERSION],
[dnl
m4_assert([$# >= 2])[]dnl
cn_prog_ver=`$1 --version 2>&AS_MESSAGE_LOG_FD | sed 1q 2>&AS_MESSAGE_LOG_FD`
AS_CASE([[$]?:$cn_prog_ver],
	[[0:*[0-9].[0-9]*]], [$2=$cn_prog_ver],
	[$2=unknown])[]dnl
])

## CN_CHECK_COMPILE_FLAGS(flags-var, description, flags)
##
## Find a compiler flag for <description>. For each flag in <flags>, check
## if the compiler for the current language accepts it. On success, stop the
## search and append the last tested flag to <flags-var>. Calls AC_SUBST
## on <flags-var>.
##
AC_DEFUN([CN_CHECK_COMPILE_FLAGS],
[dnl
m4_assert([$# >= 3])[]dnl
AC_MSG_CHECKING([compiler flag for $2])
cn_ccf_result=no
cn_ccf_save_CPPFLAGS=$CPPFLAGS
for cn_flag in $3
do
	CPPFLAGS="$cn_ccf_save_CPPFLAGS $cn_flag"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [cn_ccf_result=$cn_flag])
	test "x$cn_ccf_result" = xno || break
done
CPPFLAGS=$cn_ccf_save_CPPFLAGS
AS_IF([test "x$cn_ccf_result" != xno],
	[CN_APPEND([$1], [$cn_ccf_result])])
AC_MSG_RESULT([$cn_ccf_result])
AC_SUBST([$1])
])

## _CN_ARG_ENABLE_WARNINGS_ONCE
##
## Implementation helper macro of CN_ARG_ENABLE_WARNINGS. Pulled in
## through AC_REQUIRE so that it is only expanded once.
##
m4_define([_CN_ARG_ENABLE_WARNINGS_ONCE],
[dnl
AC_PROVIDE([$0])[]dnl
AC_ARG_ENABLE([warnings],
		[AS_HELP_STRING([[--enable-warnings[=min|max|fatal|no]]],
				[set compile pedantry level [default=max]])],
		[cn_enable_warnings=$enableval],
		[cn_enable_warnings=max])[]dnl
dnl
# Test whether the compiler accepts each flag.  Look at standard output,
# since GCC only shows a warning message if an option is not supported.
cn_check_compile_warning_flags() {
	for cn_flag
	do
		cn_cc_out=`$cn_cc $cn_warning_flags $cn_flag -c "$cn_conftest" 2>&1 || echo failed`
		AS_IF([test "$?$cn_cc_out" = 0],
			[CN_APPEND([cn_warning_flags], [$cn_flag])],
			[AS_ECHO(["$cn_cc: $cn_cc_out"]) >&AS_MESSAGE_LOG_FD])
		rm -f "conftest.[$]{OBJEXT:-o}"
	done
}
])

## CN_ARG_ENABLE_WARNINGS(variable, min-flags, max-flags)
##
## Provide the --enable-warnings configure argument, set to "min" by default.
## <min-flags> and <max-flags> should be space-separated lists of compiler
## warning flags to use with --enable-warnings=min or --enable-warnings=max,
## respectively. Warning level "fatal" is the same as "max" but in addition
## enables -Werror mode.
##
## In order to determine the warning options to use with the C++ compiler,
## call AC_LANG([C++]) first to change the current language. If different
## output variables are used, it is also fine to call CN_ARG_ENABLE_WARNINGS
## repeatedly, once for each language setting.
##
AC_DEFUN([CN_ARG_ENABLE_WARNINGS],
[dnl
m4_assert([$# >= 3])[]dnl
AC_REQUIRE([_CN_ARG_ENABLE_WARNINGS_ONCE])[]dnl
dnl
AS_CASE([$ac_compile],
	[[*'$CXXFLAGS '*]], [cn_lang='C++' cn_cc=$CXX cn_conftest="conftest.[$]{ac_ext:-cc}"],
	[[*'$CFLAGS '*]],   [cn_lang=C cn_cc=$CC cn_conftest="conftest.[$]{ac_ext:-c}"],
	[AC_MSG_ERROR([[current language is neither C nor C++]])])
dnl
AC_MSG_CHECKING([which $cn_lang compiler warning flags to use])
cn_warning_flags=
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
int main(int argc, char** argv) { return (argv != 0) ? argc : 0; }
]])])
AS_CASE([$cn_enable_warnings],
	[no], [],
	[min], [cn_check_compile_warning_flags $2],
	[fatal], [cn_check_compile_warning_flags $3 -Werror],
	[cn_check_compile_warning_flags $3])
rm -f "$cn_conftest"
AC_SUBST([$1], [$cn_warning_flags])
AC_MSG_RESULT([[$]{cn_warning_flags:-none}])[]dnl
])
