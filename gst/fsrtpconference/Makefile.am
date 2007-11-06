plugin_LTLIBRARIES = libfsrtpconference.la

libfsrtpconference_la_SOURCES = gstfsrtpconference.c \
	fs-rtp-conference.c \
	fs-rtp-participant.c \
	fs-rtp-session.c \
	fs-rtp-stream.c
libfsrtpconference_la_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libfsrtpconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsrtpconference_la_LIBADD = $(FS2_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la


noinst_HEADERS = \
	fs-rtp-conference.h \
	fs-rtp-participant.h \
	fs-rtp-session.h \
	fs-rtp-stream.h
