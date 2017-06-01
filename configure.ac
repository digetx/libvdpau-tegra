#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libvdpau_tegra], [1.0.0], [digetx@gmail.com])
AC_CONFIG_SRCDIR([src/vdpau_tegra.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([foreign subdir-objects])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG
gl_VISIBILITY

# Checks for dependencies.
PKG_CHECK_MODULES(X11, x11)
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

PKG_CHECK_MODULES(VDPAU, vdpau)
AC_SUBST(VDPAU_CFLAGS)
AC_SUBST(VDPAU_LIBS)

PKG_CHECK_MODULES(PIXMAN, pixman-1)
AC_SUBST(PIXMAN_CFLAGS)
AC_SUBST(PIXMAN_LIBS)

PKG_CHECK_MODULES(DRM, [libdrm libdrm_tegra >= 2.4.81])
AC_SUBST(DRM_CFLAGS)
AC_SUBST(DRM_LIBS)

PKG_CHECK_MODULES(XV, xv)
AC_SUBST(XV_CFLAGS)
AC_SUBST(XV_LIBS)

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

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/vdpau-tegra.pc])
AC_OUTPUT
