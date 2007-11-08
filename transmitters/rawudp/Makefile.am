
plugindir = $(FS2_PLUGIN_PATH)

plugin_LTLIBRARIES = librawudp-transmitter.la

# sources used to compile this lib
librawudp_transmitter_la_SOURCES = \
	fs-rawudp-transmitter.c \
	fs-rawudp-stream-transmitter.c \
	stun.c

# flags used to compile this plugin
librawudp_transmitter_la_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
librawudp_transmitter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
librawudp_transmitter_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)

noinst_HEADERS = \
	fs-rawudp-transmitter.h \
	fs-rawudp-stream-transmitter.h \
	stun.h
