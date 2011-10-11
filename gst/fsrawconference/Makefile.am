plugin_LTLIBRARIES = libfsrawconference.la


# First build a convenience lib with the common stuff so its not built twice

noinst_LTLIBRARIES = libfsrawconference-convenience.la

libfsrawconference_convenience_la_SOURCES = \
	fs-raw-conference.c \
	fs-raw-session.c \
	fs-raw-stream.c \
	fs-raw-participant.c

noinst_HEADERS = \
	fs-raw-conference.h \
	fs-raw-session.h \
	fs-raw-stream.h \
	fs-raw-participant.h


AM_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)

# Build the main plugin

libfsrawconference_la_SOURCES = fs-raw-conference-plugin.c

libfsrawconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsrawconference_la_LIBADD = \
	libfsrawconference-convenience.la \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(FS_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)



# Optionally build a library to allow building the gtk-doc

if ENABLE_GTK_DOC
noinst_LTLIBRARIES += libfsrawconference_doc.la

nodist_libfsrawconference_doc_la_SOURCES = \
	fs-raw-conference-plugin.doc.c \
	$(libfsrawconference_la_SOURCES)

libfsrawconference_doc_la_LIBADD = $(libfsrawconference_la_LIBADD)

%.doc.c: %.c
	$(AM_V_GEN)sed -e 's:GST_PLUGIN_DEFINE:GST_PLUGIN_DEFINE_STATIC:' 	$< > $@

BUILT_SOURCES = fs-raw-conference-plugin.doc.c

CLEANFILES = $(BUILT_SOURCES)

endif

