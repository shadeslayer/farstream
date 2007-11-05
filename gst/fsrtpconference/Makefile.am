plugin_LTLIBRARIES = libfsrtpconference.la

libfsrtpconference_la_SOURCES = \
	fs-rtp-conference.c \
	fs-rtp-participant.c \
	fs-rtp-session.c
libfsrtpconference_la_CFLAGS = $(FS2_PLUGINS_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libfsrtpconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsrtpconference_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)

noinst_HEADERS = \
	fs-rtp-conference.h \
	fs-rtp-participant.h \
	fs-rtp-session.h
