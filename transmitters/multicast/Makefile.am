
plugindir = $(FS_PLUGIN_PATH)

plugin_LTLIBRARIES = libmulticast-transmitter.la

# sources used to compile this lib
libmulticast_transmitter_la_SOURCES = \
	fs-multicast-transmitter.c \
	fs-multicast-stream-transmitter.c

# flags used to compile this plugin
libmulticast_transmitter_la_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libmulticast_transmitter_la_LDFLAGS = $(FS_PLUGIN_LDFLAGS)
libmulticast_transmitter_la_LIBADD = \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(FS_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = \
	fs-multicast-transmitter.h \
	fs-multicast-stream-transmitter.h
