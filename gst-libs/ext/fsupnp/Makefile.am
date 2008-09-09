lib_LTLIBRARIES = libfsupnp.la

fsupnpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/ext/fsupnp

fsupnpinclude_HEADERS = \
	fs-upnp-simple-igd.h \
	fs-upnp-simple-igd-thread.h

libfsupnp_la_SOURCES = \
	fs-upnp-simple-igd.c \
	fs-upnp-simple-igd-thread.c \
	fs-upnp-simple-igd-marshal.c


noinst_HEADERS = fs-upnp-simple-igd-marshal.h


BUILT_SOURCES = \
	fs-upnp-simple-igd-marshal.c \
	fs-upnp-simple-igd-marshal.h

CLEANFILES = $(BUILT_SOURCES)

fs-upnp-simple-igd-marshal.h: fs-upnp-simple-igd-marshal.list Makefile
	glib-genmarshal --header --prefix=_fs_upnp_simple_igd_marshal $(srcdir)/$< > $@.tmp
	mv $@.tmp $@

fs-upnp-simple-igd-marshal.c: fs-upnp-simple-igd-marshal.list Makefile
	echo "#include \"glib-object.h\"" >> $@.tmp
	echo "#include \"fs-upnp-simple-igd-marshal.h\"" >> $@.tmp
	glib-genmarshal --body --prefix=_fs_upnp_simple_igd_marshal $(srcdir)/$< >> $@.tmp
	mv $@.tmp $@

libfsupnp_la_CFLAGS = \
	$(GUPNP_CFLAGS)

libfsupnp_la_LIBADD = \
	$(GUPNP_LIBS)

libfsupnp_la_LDFLAGS = \
	$(GUPNP_LDFLAGS)
