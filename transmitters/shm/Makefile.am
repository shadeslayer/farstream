
plugindir = $(FS_PLUGIN_PATH)

plugin_LTLIBRARIES = libshm-transmitter.la

# sources used to compile this lib
libshm_transmitter_la_SOURCES = \
	fs-shm-transmitter.c \
	fs-shm-stream-transmitter.c

# flags used to compile this plugin
libshm_transmitter_la_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libshm_transmitter_la_LDFLAGS = $(FS_PLUGIN_LDFLAGS)
libshm_transmitter_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farstream/libfarstream-0.10.la \
	$(FS_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = \
	fs-shm-transmitter.h \
	fs-shm-stream-transmitter.h
