plugin_LTLIBRARIES = libfsmsnconference.la

libfsmsnconference_la_SOURCES = \
	fs-msn-conference.c \
	fs-msn-participant.c \
	fs-msn-session.c \
	fs-msn-connection.c \
	fs-msn-stream.c 

BUILT_SOURCES =  

noinst_HEADERS = \
	fs-msn-conference.h \
	fs-msn-participant.h \
	fs-msn-session.h \
	fs-msn-connection.h  \
	fs-msn-stream.h 

EXTRA_libfsmsnconference_la_SOURCES = 

CLEANFILES = $(BUILT_SOURCES)

libfsmsnconference_la_CFLAGS = \
	$(FS2_INTERNAL_CFLAGS) \
	$(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(NICE_CFLAGS)

libfsmsnconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsmsnconference_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(NICE_LIBS)

check_PROGRAMS = test-connection

test_connection_SOURCES = test-connection.c fs-msn-connection.c
test_connection_CFLAGS = \
        $(FS2_INTERNAL_CFLAGS) \
        $(FS2_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_CFLAGS) \
	$(NICE_CFLAGS)
test_connection_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
test_connection_LDADD = \
        $(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
        $(FS2_LIBS) \
        $(GST_BASE_LIBS) \
        $(GST_LIBS) \
	$(NICE_LIBS)

