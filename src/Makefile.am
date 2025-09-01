plugindir = /usr/lib/x86_64-linux-gnu/purple-2
icondir = /usr/share/pixmaps/pidgin/protocols/16

plugin_LTLIBRARIES = libmistral.la

libmistral_la_SOURCES = mistral.c
libmistral_la_LDFLAGS = -module -avoid-version $(AM_LDFLAGS)
libmistral_la_LIBADD = $(PURPLE_LIBS) $(JSON_LIBS) $(CURL_LIBS)

AM_CPPFLAGS = $(PURPLE_CFLAGS) $(JSON_CFLAGS) $(CURL_CFLAGS) $(GTK_CFLAGS)
AM_CFLAGS = $(CURL_CFLAGS) $(GTK_CFLAGS)

icon_DATA = mistral-logo.png
include $(top_srcdir)/plugins.mk
