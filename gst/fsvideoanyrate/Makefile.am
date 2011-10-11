plugin_LTLIBRARIES = libfsvideoanyrate.la

libfsvideoanyrate_la_SOURCES = videoanyrate.c
libfsvideoanyrate_la_CFLAGS = \
	$(FS_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libfsvideoanyrate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsvideoanyrate_la_LIBADD = \
	$(FS_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = videoanyrate.h
