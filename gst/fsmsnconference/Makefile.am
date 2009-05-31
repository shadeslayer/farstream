plugin_LTLIBRARIES = libfsmsnconference.la


# First build a convenience lib with the common stuff so its not built twice

noinst_LTLIBRARIES = libfsmsnconference-convenience.la

libfsmsnconference_convenience_la_SOURCES = \
	fs-msn-participant.c \
	fs-msn-session.c \
	fs-msn-connection.c \
	fs-msn-stream.c 

noinst_HEADERS = \
	fs-msn-conference.h \
	fs-msn-participant.h \
	fs-msn-session.h \
	fs-msn-connection.h  \
	fs-msn-stream.h 


AM_CFLAGS = \
	$(FS2_INTERNAL_CFLAGS) \
	$(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)

# Build the main plugin

libfsmsnconference_la_SOURCES = fs-msn-conference.c

libfsmsnconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsmsnconference_la_LIBADD = \
	libfsmsnconference-convenience.la \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)



# Optionally build a library to allow building the gtk-doc

if ENABLE_GTK_DOC
noinst_LTLIBRARIES += libfsmsnconference_doc.la

nodist_libfsmsnconference_doc_la_SOURCES = \
	fs-msn-conference-doc.c

libfsmsnconference_doc_la_LIBADD = $(libfsmsnconference_la_LIBADD)

fs-msn-conference-doc.c: fs-msn-conference.c
	sed -e 's:GST_PLUGIN_DEFINE:GST_PLUGIN_DEFINE_STATIC:' \
		$(srcdir)/fs-msn-conference.c > fs-msn-conference-doc.c

BUILT_SOURCES = fs-msn-conference-doc.c
CLEANFILES = $(BUILT_SOURCES)

endif

