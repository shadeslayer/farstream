
noinst_PROGRAMS = codec-discovery

codec_discovery_SOURCES = codec-discovery.c \
		$(top_srcdir)/gst/fsrtpconference/fs-rtp-discover-codecs.c \
		$(top_srcdir)/gst/fsrtpconference/fs-rtp-codec-cache.c
codec_discovery_CFLAGS = -I$(top_srcdir)/gst/fsrtpconference/ \
	$(FS2_INTERNAL_CFLAGS) $(FS2_CFLAGS) $(GST_CFLAGS)  $(CFLAGS) \
	-DSTANDALONE
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
