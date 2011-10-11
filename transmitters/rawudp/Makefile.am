
plugindir = $(FS_PLUGIN_PATH)

plugin_LTLIBRARIES = librawudp-transmitter.la

# sources used to compile this lib
librawudp_transmitter_la_SOURCES = \
	fs-rawudp-transmitter.c \
	fs-rawudp-stream-transmitter.c \
	fs-rawudp-component.c

nodist_librawudp_transmitter_la_SOURCES = \
	fs-rawudp-marshal.c \
	fs-rawudp-marshal.h


# flags used to compile this plugin
librawudp_transmitter_la_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(NICE_CFLAGS) \
	$(GUPNP_CFLAGS)
librawudp_transmitter_la_LDFLAGS = $(FS_PLUGIN_LDFLAGS)
librawudp_transmitter_la_LIBADD = \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(FS_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(NICE_LIBS) \
	$(GUPNP_LIBS) \
	-lgstnetbuffer-@GST_MAJORMINOR@

noinst_HEADERS = \
	fs-rawudp-transmitter.h \
	fs-rawudp-stream-transmitter.h \
	fs-rawudp-component.h

BUILT_SOURCES = $(nodist_librawudp_transmitter_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES) fs-rawudp-marshal.list


fs-rawudp-marshal.list: $(librawudp_transmitter_la_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*_fs_rawudp_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(librawudp_transmitter_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

glib_enum_define=FS_RAWUDP
glib_gen_prefix=_fs_rawudp
glib_gen_basename=fs-rawudp

include $(top_srcdir)/common/gst-glib-gen.mak

