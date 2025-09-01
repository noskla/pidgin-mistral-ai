AC_PREREQ([2.69])
AC_INIT([pidgin-mistral], [1.0], [your.email@example.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

PKG_CHECK_MODULES([PURPLE], [purple >= 2.0.0])

PKG_CHECK_MODULES([JSON], [json-glib-1.0], [], [
    AC_MSG_ERROR([json-glib-1.0 is required but not found])
])

PKG_CHECK_MODULES([CURL], [libcurl], [], [
    AC_MSG_ERROR([libcurl is required but not found])
])

PKG_CHECK_MODULES([GTK], [gtk+-3.0], [], [
    AC_MSG_ERROR([GTK+ 3.0 is required but not found])
])

AM_PROG_AR
LT_INIT
AC_PROG_CC

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
