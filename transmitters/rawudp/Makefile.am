
plugindir = $(FS2_PLUGIN_PATH)

plugin_LTLIBRARIES = librawudp-transmitter.la

# sources used to compile this lib
librawudp_transmitter_la_SOURCES = \
	fs-rawudp-transmitter.c \
	fs-rawudp-stream-transmitter.c \
	fs-rawudp-component.c \
	stun.c

nodist_librawudp_transmitter_la_SOURCES = \
	fs-rawudp-marshal.c \
	fs-rawudp-marshal.h


# flags used to compile this plugin
librawudp_transmitter_la_CFLAGS = \
	$(FS2_INTERNAL_CFLAGS) \
	$(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GUPNP_CFLAGS)
librawudp_transmitter_la_LDFLAGS = $(FS2_PLUGIN_LDFLAGS)
librawudp_transmitter_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(GUPNP_LIBS) \
	-lgstnetbuffer-@GST_MAJORMINOR@

noinst_HEADERS = \
	fs-rawudp-transmitter.h \
	fs-rawudp-stream-transmitter.h \
	fs-rawudp-component.h \
	stun.h

BUILT_SOURCES = $(nodist_librawudp_transmitter_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES) fs-rawudp-marshal.list


fs-rawudp-marshal.list: $(librawudp_transmitter_la_SOURCES) Makefile.am
	( cd $(srcdir) && \
	sed -n -e 's/.*_fs_rawudp_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(librawudp_transmitter_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

glib_enum_define=FS_RAWUDP
glib_enum_prefix=_fs_rawudp

include $(top_srcdir)/common/glib-gen.mak

