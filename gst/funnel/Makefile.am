plugin_LTLIBRARIES = libfsfunnel.la

libfsfunnel_la_SOURCES = gstfsfunnel.c
libfsfunnel_la_CFLAGS = \
	$(FS2_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libfsfunnel_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsfunnel_la_LIBADD = \
	$(FS2_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = gstfsfunnel.h
