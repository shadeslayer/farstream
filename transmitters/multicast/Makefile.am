
plugindir = $(FS2_PLUGIN_PATH)

plugin_LTLIBRARIES = libmulticast-transmitter.la

# sources used to compile this lib
libmulticast_transmitter_la_SOURCES = \
	fs-multicast-transmitter.c \
	fs-multicast-stream-transmitter.c

# flags used to compile this plugin
libmulticast_transmitter_la_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libmulticast_transmitter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libmulticast_transmitter_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)

noinst_HEADERS = \
	fs-multicast-transmitter.h \
	fs-multicast-stream-transmitter.h
