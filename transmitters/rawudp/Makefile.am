
plugindir = $(FS2_PLUGIN_PATH)

plugin_LTLIBRARIES = librawudp-transmitter.la

# sources used to compile this lib
librawudp_transmitter_la_SOURCES = \
	fs-rawudp-transmitter.c \
	fs-rawudp-stream-transmitter.c \
	fs-rawudp-component.c \
	fs-interfaces.c \
	fs-rawudp-marshal.c \
	stun.c

# flags used to compile this plugin
librawudp_transmitter_la_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
librawudp_transmitter_la_LDFLAGS = $(FS2_PLUGIN_LDFLAGS)
librawudp_transmitter_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)

EXTRA_librawudp_transmitter_la_SOURCES = fs-rawudp-marshal.list

noinst_HEADERS = \
	fs-rawudp-transmitter.h \
	fs-rawudp-stream-transmitter.h \
	fs-rawudp-component.h \
	fs-interfaces.h \	
	fs-rawudp-marshal.h \
	stun.h

BUILT_SOURCES = \
		fs-rawudp-marshal.c \
		fs-rawudp-marshal.h

CLEANFILES = $(BUILT_SOURCES)


fs-rawudp-marshal.h: fs-rawudp-marshal.list Makefile
		glib-genmarshal --header --prefix=_fs_rawudp_marshal $(srcdir)/$< > $@.tmp
		mv $@.tmp $@

fs-rawudp-marshal.c: fs-rawudp-marshal.list Makefile
		echo "#include \"glib-object.h\"" >> $@.tmp
		echo "#include \"fs-rawudp-marshal.h\"" >> $@.tmp
		glib-genmarshal --body --prefix=_fs_rawudp_marshal $(srcdir)/$< >> $@.tmp
		mv $@.tmp $@
