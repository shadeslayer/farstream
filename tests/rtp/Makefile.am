
noinst_PROGRAMS = codec-discovery

codec_discovery_SOURCES = codec-discovery.c
codec_discovery_CFLAGS = \
	-I$(top_srcdir)/gst/fsrtpconference/ \
	-I$(top_builddir)/gst/fsrtpconference/ \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(CFLAGS)

LDADD = \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(top_builddir)/gst/fsrtpconference/libfsrtpconference-convenience.la \
	$(GST_CHECK_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstrtp-@GST_MAJORMINOR@
