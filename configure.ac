# Mandatory, inits autoconf
AC_INIT(hildon-welcome, 0.21)

# Tests that source dir exists
AC_CONFIG_SRCDIR([src/main.c])

# Initialize automake
AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PROG_INSTALL
AC_C_CONST
AC_HEADER_STDC

AC_ARG_ENABLE([maemo-launcher],
              [AS_HELP_STRING([--enable-maemo-launcher],
                              [build with maemo-launcher support])],
                              [case "${enableval}" in
                               yes) maemo_launcher=true ;;
                               no)  maemo_launcher=false ;;
                               *) AC_MSG_ERROR([bad value ${enableval} for --enable-maemo-launcher]) ;;
                               esac], [maemo_launcher=false])

DBUS_PACKAGE=""
MCE_PACKAGE=""
MAEMO_LAUNCHER_PACKAGE=""

PKG_CHECK_EXISTS( mce,
  [ 
  AC_DEFINE(HAVE_MCE)
  MCE_PACKAGE="mce"
  DBUS_PACKAGE="dbus-1"
  ])

if test "x$maemo_launcher" = "xtrue"; then
  MAEMO_LAUNCHER_PACKAGE="maemo-launcher-app"
fi

PKG_CHECK_MODULES(HILDON_WELCOME_DEPS,
  [
  x11
  gstreamer-0.10 >= 0.10.0
  gstreamer-interfaces-0.10 >= 0.10.0
  gstreamer-plugins-base-0.10 >= 0.10.0
  xcomposite
  profile
  $DBUS_PACKAGE
  $MCE_PACKAGE
  $MAEMO_LAUNCHER_PACKAGE
  ])

AC_SUBST(HILDON_WELCOME_DEPS_LIBS)
AC_SUBST(HILDON_WELCOME_DEPS_CFLAGS)
AC_SUBST(PACKAGE_NAME)

# Define output files
AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])

# Mandatory, produce output
AC_OUTPUT
