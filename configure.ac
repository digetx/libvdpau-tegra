#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([libvdpau_tegra], [1.0.0], [digetx@gmail.com])
AC_CONFIG_SRCDIR([src/vdpau_tegra.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([foreign subdir-objects])

AM_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG
gl_VISIBILITY

# Checks for dependencies.
PKG_CHECK_MODULES(X11, x11 xext xextproto xfixes xrandr)
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

PKG_CHECK_MODULES(VDPAU, vdpau)
AC_SUBST(VDPAU_CFLAGS)
AC_SUBST(VDPAU_LIBS)

PKG_CHECK_MODULES(PIXMAN, pixman-1)
AC_SUBST(PIXMAN_CFLAGS)
AC_SUBST(PIXMAN_LIBS)

PKG_CHECK_MODULES(DRM, [libdrm >= 2.4.81])
AC_SUBST(DRM_CFLAGS)
AC_SUBST(DRM_LIBS)

PKG_CHECK_MODULES(XV, xv)
AC_SUBST(XV_CFLAGS)
AC_SUBST(XV_LIBS)

dnl ==========================================================================
dnl define PKG_CHECK_VAR for old pkg-config <= 0.28
m4_ifndef([PKG_CHECK_VAR], [
AC_DEFUN([PKG_CHECK_VAR],
[AC_REQUIRE([PKG_PROG_PKG_CONFIG])dnl
AC_ARG_VAR([$1], [value of $3 for $2, overriding pkg-config])dnl

_PKG_CONFIG([$1], [variable="][$3]["], [$2])
AS_VAR_COPY([$1], [pkg_cv_][$1])

AS_VAR_IF([$1], [""], [$5], [$4])dnl
])dnl PKG_CHECK_VAR
])
dnl ==========================================================================

PKG_CHECK_VAR([udevdir], [udev], [udevdir], [], [udevdir=/lib/udev])

# Checks for libraries.
AC_CHECK_LIB([m], [sin])

# Checks for header files.
AC_PATH_X
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([clock_gettime memset munmap])

# Initialize compiler compiler
AC_PROG_LEX
if test "$LEX" = :; then
	AC_MSG_ERROR([flex not found])
fi

AC_CHECK_PROGS(YACC, 'bison -y' byacc yacc, :)
if test "$YACC" = :; then
	AC_MSG_ERROR([bison not found])
fi

SAVE_CFLAGS=$CFLAGS
SAVE_LIBS=$LIBS
CFLAGS=$DRM_CFLAGS
LIBS=$DRM_LIBS

AC_CHECK_FUNCS(
	[drmSyncobjCreate],
	[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_LIBDRM_SYNCOBJ_SUPPORT, [], [Does libdrm have drmSyncobjCreate])
	],
	[
		AC_MSG_RESULT([no])
	])

CFLAGS=$SAVE_CFLAGS
LIBS=$SAVE_LIBS

AC_ARG_ENABLE(valgrind,
	      [AS_HELP_STRING([--enable-valgrind],
	      [Build libdrm with  valgrind support (default: auto)])],
	      [VALGRIND=$enableval], [VALGRIND=auto])
if test "x$VALGRIND" != xno; then
	PKG_CHECK_MODULES(VALGRIND, [valgrind], [have_valgrind=yes], [have_valgrind=no])
fi
AC_MSG_CHECKING([whether to enable Valgrind support])
if test "x$VALGRIND" = xauto; then
	VALGRIND="$have_valgrind"
fi

if test "x$VALGRIND" = "xyes"; then
	if ! test "x$have_valgrind" = xyes; then
		AC_MSG_ERROR([Valgrind support required but not present])
	fi
	AC_DEFINE([HAVE_VALGRIND], 1, [Use valgrind intrinsics to suppress false warnings])
else
	AC_DEFINE([HAVE_VALGRIND], 0)
fi

AC_MSG_RESULT([$VALGRIND])

AC_ARG_ENABLE([debug],
	      [AS_HELP_STRING([--enable-debug],
	      [use debug compiler flags and macros @<:@default=disabled@:>@])],
	      [enable_debug="$enableval"],
	      [enable_debug=no]
)

if test "x$enable_debug" = xyes; then
	DEFINES="$DEFINES -DDEBUG"
	if test "x$GCC" = xyes; then
		if ! echo "$CFLAGS" | grep -q -e '-g'; then
			CFLAGS="$CFLAGS -g"
		fi
		if ! echo "$CFLAGS" | grep -q -e '-O'; then
			CFLAGS="$CFLAGS -O0"
		fi
	fi
	if test "x$GXX" = xyes; then
		if ! echo "$CXXFLAGS" | grep -q -e '-g'; then
			CXXFLAGS="$CXXFLAGS -g"
		fi
		if ! echo "$CXXFLAGS" | grep -q -e '-O'; then
			CXXFLAGS="$CXXFLAGS -O0"
		fi
	fi
else
	DEFINES="$DEFINES -DNDEBUG"
fi

AC_SUBST([DEFINES])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/vdpau-tegra.pc])
AC_OUTPUT
